#include "modules/rmlPreview/RmlRuntime.h"

#include "core/MaterialLibrary.h"
#include "po_catalog.h"

#include <RmlUi/Core.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QImage>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLExtraFunctions>
#include <QSet>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <memory>

namespace rmlp
{
    namespace
    {
        RmlSink* gSink = nullptr;

        QString resolvePath(const Rml::String& path)
        {
            QString requested = QString::fromStdString(path);
            // RmlUi's URLs spell a drive as "C|/"
            if (requested.size() > 2 && requested.at(1) == '|' && requested.at(0).isLetter())
                requested[1] = ':';
            const QFileInfo direct(requested);
            if (direct.isAbsolute())
                return direct.exists() ? direct.absoluteFilePath() : QString();
            if (gSink)
                for (const QString& root : gSink->rmlSearchRoots())
                {
                    const QFileInfo candidate(QDir(root).filePath(requested));
                    if (candidate.isFile())
                        return candidate.absoluteFilePath();
                }
            return {};
        }

        bool readFile(const Rml::String& path, QByteArray& out)
        {
            const QString resolved = resolvePath(path);
            QFile file(resolved);
            if (resolved.isEmpty() || !file.open(QIODevice::ReadOnly))
                return false;
            out = file.readAll();
            if (gSink)
                gSink->rmlFileRead(resolved);
            return true;
        }

        class SystemInterface final : public Rml::SystemInterface
        {
        public:
            bool LogMessage(Rml::Log::Type type, const Rml::String& message) override
            {
                const bool error = type == Rml::Log::LT_ERROR || type == Rml::Log::LT_ASSERT;
                if (gSink && (error || type == Rml::Log::LT_WARNING))
                    gSink->rmlLog(error, QString::fromStdString(message));
                return true;
            }

            // Same rule as the engine's UiSystem: a whole string that is a #Key reference is translated.
            int TranslateString(Rml::String& translated, const Rml::String& input) override
            {
                size_t begin = 0;
                size_t end = input.size();
                while (begin < end && std::isspace(static_cast<unsigned char>(input[begin])))
                    ++begin;
                while (end > begin && std::isspace(static_cast<unsigned char>(input[end - 1])))
                    --end;
                const std::string_view text(input.data() + begin, end - begin);
                if (!LocalizationIsKeyReference(text))
                {
                    translated = input;
                    return 0;
                }
                const PoCatalog* catalog = gSink ? gSink->rmlCatalog() : nullptr;
                const std::string* found = catalog ? catalog->Find(LocalizationStripKey(text)) : nullptr;
                translated.assign(input, 0, begin);
                translated += found ? *found : "#" + std::string(LocalizationStripKey(text));
                translated.append(input, end, Rml::String::npos);
                return 1;
            }
        };

        class FileInterface final : public Rml::FileInterface
        {
        public:
            Rml::FileHandle Open(const Rml::String& path) override
            {
                auto file = std::make_unique<OpenFile>();
                if (!readFile(path, file->data))
                    return 0;
                return reinterpret_cast<Rml::FileHandle>(file.release());
            }

            void Close(Rml::FileHandle handle) override { delete get(handle); }

            size_t Read(void* buffer, size_t size, Rml::FileHandle handle) override
            {
                OpenFile* file = get(handle);
                const size_t count = std::min(size, size_t(file->data.size()) - file->position);
                if (count > 0)
                    std::memcpy(buffer, file->data.constData() + file->position, count);
                file->position += count;
                return count;
            }

            bool Seek(Rml::FileHandle handle, long offset, int origin) override
            {
                OpenFile* file = get(handle);
                long long base = 0;
                if (origin == SEEK_CUR)
                    base = static_cast<long long>(file->position);
                else if (origin == SEEK_END)
                    base = file->data.size();
                const long long target = base + offset;
                if (target < 0 || target > file->data.size())
                    return false;
                file->position = size_t(target);
                return true;
            }

            size_t Tell(Rml::FileHandle handle) override { return get(handle)->position; }
            size_t Length(Rml::FileHandle handle) override { return size_t(get(handle)->data.size()); }

            bool LoadFile(const Rml::String& path, Rml::String& out) override
            {
                QByteArray data;
                if (!readFile(path, data))
                {
                    Rml::Log::Message(Rml::Log::LT_ERROR, "Could not load '%s'", path.c_str());
                    return false;
                }
                out.assign(data.constData(), size_t(data.size()));
                return true;
            }

        private:
            struct OpenFile
            {
                QByteArray data;
                size_t position = 0;
            };
            static OpenFile* get(Rml::FileHandle handle) { return reinterpret_cast<OpenFile*>(handle); }
        };

        const char* kVertexShader = R"(#version 330 core
uniform mat4 uTransform;
uniform vec2 uTranslate;
layout(location = 0) in vec2 inPosition;
layout(location = 1) in vec4 inColor;
layout(location = 2) in vec2 inUv;
out vec4 vColor;
out vec2 vUv;
void main()
{
    vColor = inColor;
    vUv = inUv;
    gl_Position = uTransform * vec4(inPosition + uTranslate, 0.0, 1.0);
}
)";

        const char* kFragmentShader = R"(#version 330 core
uniform sampler2D uTexture;
in vec4 vColor;
in vec2 vUv;
out vec4 outColor;
void main()
{
    outColor = texture(uTexture, vUv) * vColor;
}
)";

        // Premultiplied alpha throughout, as RmlUi 6 expects.
        class RenderInterface final : public Rml::RenderInterface
        {
        public:
            struct Geometry
            {
                GLuint vbo = 0;
                GLuint ibo = 0;
                GLsizei count = 0;
            };

            void beginFrame(GLuint vao, int fbHeight, int vx, int vy, float scale, int cw, int ch)
            {
                m_vao = vao;
                m_fbHeight = fbHeight;
                m_vx = vx;
                m_vy = vy;
                m_scale = scale;
                m_cw = cw;
                m_ch = ch;
                m_projection = Rml::Matrix4f::ProjectOrtho(0.0f, float(cw), float(ch), 0.0f, -10000.0f, 10000.0f);
                m_transform = m_projection;
                m_scissorEnabled = false;

                QOpenGLExtraFunctions* f = gl();
                if (!ensureProgram(f))
                    return;
                f->glDisable(GL_DEPTH_TEST);
                f->glDisable(GL_CULL_FACE);
                f->glEnable(GL_BLEND);
                f->glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
                f->glEnable(GL_SCISSOR_TEST);
                applyScissor(f);
                f->glUseProgram(m_program);
                f->glUniform1i(m_texLoc, 0);
                f->glActiveTexture(GL_TEXTURE0);
                f->glBindVertexArray(m_vao);
            }

            void endFrame()
            {
                QOpenGLExtraFunctions* f = gl();
                f->glBindVertexArray(0);
                f->glUseProgram(0);
                f->glDisable(GL_SCISSOR_TEST);
                f->glDisable(GL_BLEND);
                m_vao = 0;
            }

            void destroy()
            {
                if (!QOpenGLContext::currentContext())
                    return;
                QOpenGLExtraFunctions* f = gl();
                if (m_program)
                    f->glDeleteProgram(m_program);
                if (m_white)
                    f->glDeleteTextures(1, &m_white);
                m_program = 0;
                m_white = 0;
            }

            Rml::CompiledGeometryHandle CompileGeometry(Rml::Span<const Rml::Vertex> vertices, Rml::Span<const int> indices) override
            {
                runtime::ensureCurrentContext();
                QOpenGLExtraFunctions* f = gl();
                auto* geometry = new Geometry;
                f->glGenBuffers(1, &geometry->vbo);
                f->glGenBuffers(1, &geometry->ibo);
                GLint previousArray = 0;
                f->glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &previousArray);
                f->glBindBuffer(GL_ARRAY_BUFFER, geometry->vbo);
                f->glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(vertices.size() * sizeof(Rml::Vertex)), vertices.data(), GL_STATIC_DRAW);
                f->glBindBuffer(GL_ARRAY_BUFFER, GLuint(previousArray));
                // Element buffer bindings belong to the VAO; compile with none bound.
                GLint previousVao = 0;
                f->glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &previousVao);
                f->glBindVertexArray(0);
                f->glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, geometry->ibo);
                f->glBufferData(GL_ELEMENT_ARRAY_BUFFER, GLsizeiptr(indices.size() * sizeof(int)), indices.data(), GL_STATIC_DRAW);
                f->glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
                f->glBindVertexArray(GLuint(previousVao));
                geometry->count = GLsizei(indices.size());
                return reinterpret_cast<Rml::CompiledGeometryHandle>(geometry);
            }

            void ReleaseGeometry(Rml::CompiledGeometryHandle handle) override
            {
                auto* geometry = reinterpret_cast<Geometry*>(handle);
                runtime::ensureCurrentContext();
                QOpenGLExtraFunctions* f = gl();
                f->glDeleteBuffers(1, &geometry->vbo);
                f->glDeleteBuffers(1, &geometry->ibo);
                delete geometry;
            }

            void RenderGeometry(Rml::CompiledGeometryHandle handle, Rml::Vector2f translation, Rml::TextureHandle texture) override
            {
                if (!m_vao || !m_program)
                    return;
                auto* geometry = reinterpret_cast<Geometry*>(handle);
                QOpenGLExtraFunctions* f = gl();
                f->glUniformMatrix4fv(m_transformLoc, 1, GL_FALSE, m_transform.data());
                f->glUniform2f(m_translateLoc, translation.x, translation.y);
                f->glBindTexture(GL_TEXTURE_2D, texture ? GLuint(texture) : m_white);
                f->glBindBuffer(GL_ARRAY_BUFFER, geometry->vbo);
                f->glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, geometry->ibo);
                f->glEnableVertexAttribArray(0);
                f->glEnableVertexAttribArray(1);
                f->glEnableVertexAttribArray(2);
                f->glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Rml::Vertex), reinterpret_cast<void*>(offsetof(Rml::Vertex, position)));
                f->glVertexAttribPointer(1, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(Rml::Vertex), reinterpret_cast<void*>(offsetof(Rml::Vertex, colour)));
                f->glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Rml::Vertex), reinterpret_cast<void*>(offsetof(Rml::Vertex, tex_coord)));
                f->glDrawElements(GL_TRIANGLES, geometry->count, GL_UNSIGNED_INT, nullptr);
            }

            Rml::TextureHandle LoadTexture(Rml::Vector2i& dimensions, const Rml::String& source) override
            {
                const QString resolved = resolvePath(source);
                if (resolved.isEmpty())
                {
                    Rml::Log::Message(Rml::Log::LT_WARNING, "Could not load image '%s'", source.c_str());
                    return 0;
                }
                if (gSink)
                    gSink->rmlFileRead(resolved);
                // .ktex goes through Forge's reader, everything else through Qt
                QString error;
                QImage image = MaterialLibrary::loadTexture(resolved, &error);
                if (image.isNull())
                {
                    Rml::Log::Message(Rml::Log::LT_WARNING, "Could not load image '%s': %s", source.c_str(), error.toUtf8().constData());
                    return 0;
                }
                image = image.convertToFormat(QImage::Format_RGBA8888_Premultiplied);
                dimensions = { image.width(), image.height() };
                return upload(image.constBits(), image.width(), image.height(), int(image.bytesPerLine()));
            }

            Rml::TextureHandle GenerateTexture(Rml::Span<const Rml::byte> source, Rml::Vector2i dimensions) override
            {
                return upload(source.data(), dimensions.x, dimensions.y, dimensions.x * 4);
            }

            void ReleaseTexture(Rml::TextureHandle texture) override
            {
                runtime::ensureCurrentContext();
                GLuint id = GLuint(texture);
                gl()->glDeleteTextures(1, &id);
            }

            void EnableScissorRegion(bool enable) override
            {
                m_scissorEnabled = enable;
                if (m_vao)
                    applyScissor(gl());
            }

            void SetScissorRegion(Rml::Rectanglei region) override
            {
                m_scissor = region;
                if (m_vao)
                    applyScissor(gl());
            }

            void SetTransform(const Rml::Matrix4f* transform) override
            {
                m_transform = transform ? m_projection * *transform : m_projection;
            }

        private:
            static QOpenGLExtraFunctions* gl() { return QOpenGLContext::currentContext()->extraFunctions(); }

            void applyScissor(QOpenGLExtraFunctions* f) const
            {
                Rml::Rectanglei r = m_scissorEnabled ? m_scissor : Rml::Rectanglei::FromSize({ m_cw, m_ch });
                const int left = m_vx + int(std::floor(r.Left() * m_scale));
                const int right = m_vx + int(std::ceil(r.Right() * m_scale));
                const int top = m_vy + int(std::floor(r.Top() * m_scale));
                const int bottom = m_vy + int(std::ceil(r.Bottom() * m_scale));
                f->glScissor(left, m_fbHeight - bottom, std::max(0, right - left), std::max(0, bottom - top));
            }

            Rml::TextureHandle upload(const void* pixels, int width, int height, int stride)
            {
                runtime::ensureCurrentContext();
                QOpenGLExtraFunctions* f = gl();
                GLuint id = 0;
                f->glGenTextures(1, &id);
                GLint previous = 0;
                f->glGetIntegerv(GL_TEXTURE_BINDING_2D, &previous);
                f->glBindTexture(GL_TEXTURE_2D, id);
                f->glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
                f->glPixelStorei(GL_UNPACK_ROW_LENGTH, stride / 4);
                f->glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
                f->glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
                f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
                f->glBindTexture(GL_TEXTURE_2D, GLuint(previous));
                return Rml::TextureHandle(id);
            }

            GLuint compile(QOpenGLExtraFunctions* f, GLenum type, const char* source)
            {
                const GLuint shader = f->glCreateShader(type);
                f->glShaderSource(shader, 1, &source, nullptr);
                f->glCompileShader(shader);
                GLint ok = 0;
                f->glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
                if (!ok)
                {
                    char log[1024] = {};
                    f->glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
                    Rml::Log::Message(Rml::Log::LT_ERROR, "Preview shader: %s", log);
                }
                return shader;
            }

            bool ensureProgram(QOpenGLExtraFunctions* f)
            {
                if (m_program)
                    return true;
                const GLuint vs = compile(f, GL_VERTEX_SHADER, kVertexShader);
                const GLuint fs = compile(f, GL_FRAGMENT_SHADER, kFragmentShader);
                m_program = f->glCreateProgram();
                f->glAttachShader(m_program, vs);
                f->glAttachShader(m_program, fs);
                f->glLinkProgram(m_program);
                f->glDeleteShader(vs);
                f->glDeleteShader(fs);
                GLint ok = 0;
                f->glGetProgramiv(m_program, GL_LINK_STATUS, &ok);
                if (!ok)
                {
                    f->glDeleteProgram(m_program);
                    m_program = 0;
                    return false;
                }
                m_transformLoc = f->glGetUniformLocation(m_program, "uTransform");
                m_translateLoc = f->glGetUniformLocation(m_program, "uTranslate");
                m_texLoc = f->glGetUniformLocation(m_program, "uTexture");
                const uint32_t white = 0xffffffffu;
                m_white = GLuint(upload(&white, 1, 1, 4));
                return true;
            }

            GLuint m_program = 0;
            GLuint m_white = 0;
            GLint m_transformLoc = -1;
            GLint m_translateLoc = -1;
            GLint m_texLoc = -1;

            GLuint m_vao = 0;
            int m_fbHeight = 0;
            int m_vx = 0;
            int m_vy = 0;
            float m_scale = 1.0f;
            int m_cw = 1;
            int m_ch = 1;
            Rml::Matrix4f m_projection = Rml::Matrix4f::Identity();
            Rml::Matrix4f m_transform = Rml::Matrix4f::Identity();
            bool m_scissorEnabled = false;
            Rml::Rectanglei m_scissor;
        };

        // onclick="name args", as in the engine; the preview only reports the action.
        class ActionListener final : public Rml::EventListener
        {
        public:
            ActionListener(QString name, QString args) : m_name(std::move(name)), m_args(std::move(args)) {}
            void ProcessEvent(Rml::Event& event) override;
            void OnDetach(Rml::Element*) override { delete this; }

        private:
            QString m_name;
            QString m_args;
        };

        class ActionInstancer final : public Rml::EventListenerInstancer
        {
        public:
            Rml::EventListener* InstanceEventListener(const Rml::String& value, Rml::Element*) override
            {
                const QString text = QString::fromStdString(value).trimmed();
                if (text.isEmpty())
                    return nullptr;
                const qsizetype space = text.indexOf(' ');
                return new ActionListener(space < 0 ? text : text.left(space), space < 0 ? QString() : text.mid(space + 1).trimmed());
            }
        };

        struct State
        {
            int users = 0;
            SystemInterface system;
            FileInterface files;
            RenderInterface render;
            ActionInstancer actions;
            QSet<QString> fonts;
            QHash<Rml::Context*, RmlSink*> contexts;
            std::unique_ptr<QOffscreenSurface> surface;
            std::unique_ptr<QOpenGLContext> context;
        };

        State& state()
        {
            static State s;
            return s;
        }

        void ActionListener::ProcessEvent(Rml::Event& event)
        {
            Rml::Element* element = event.GetCurrentElement();
            Rml::Context* context = element ? element->GetContext() : nullptr;
            if (RmlSink* sink = state().contexts.value(context, nullptr))
                sink->rmlAction(m_name, m_args);
        }
    }

    namespace runtime
    {
        bool acquire()
        {
            State& s = state();
            if (s.users++ > 0)
                return true;
            Rml::SetSystemInterface(&s.system);
            Rml::SetFileInterface(&s.files);
            Rml::SetRenderInterface(&s.render);
            if (!Rml::Initialise())
            {
                s.users = 0;
                return false;
            }
            Rml::Factory::RegisterEventListenerInstancer(&s.actions);
            return true;
        }

        void release()
        {
            State& s = state();
            if (s.users <= 0 || --s.users > 0)
                return;
            ensureCurrentContext();
            Rml::Shutdown();
            s.render.destroy();
            s.fonts.clear();
            s.contexts.clear();
            if (s.context && QOpenGLContext::currentContext() == s.context.get())
                s.context->doneCurrent();
            s.context.reset();
            s.surface.reset();
        }

        void ensureCurrentContext()
        {
            if (QOpenGLContext::currentContext())
                return;
            State& s = state();
            if (!s.context)
            {
                s.surface = std::make_unique<QOffscreenSurface>();
                s.surface->create();
                s.context = std::make_unique<QOpenGLContext>();
                s.context->setShareContext(QOpenGLContext::globalShareContext());
                s.context->create();
            }
            s.context->makeCurrent(s.surface.get());
        }

        void loadFonts(const QStringList& paths, const QStringList& fallbackPaths)
        {
            State& s = state();
            int index = 0;
            const auto load = [&s](const QString& path, bool fallback)
            {
                if (s.fonts.contains(path))
                    return;
                if (Rml::LoadFontFace(path.toStdString(), fallback))
                    s.fonts.insert(path);
            };
            for (const QString& path : paths)
                load(path, index++ == 0);
            for (const QString& path : fallbackPaths)
                load(path, true);
        }

        void registerContext(Rml::Context* context, RmlSink* sink)
        {
            state().contexts.insert(context, sink);
        }

        void unregisterContext(Rml::Context* context)
        {
            state().contexts.remove(context);
        }

        void beginFrame(unsigned vao, int framebufferHeight, int viewportX, int viewportY, float scale, int contextWidth, int contextHeight)
        {
            state().render.beginFrame(vao, framebufferHeight, viewportX, viewportY, scale, contextWidth, contextHeight);
        }

        void endFrame()
        {
            state().render.endFrame();
        }
    }

    SinkScope::SinkScope(RmlSink* sink) : m_previous(gSink)
    {
        gSink = sink;
    }

    SinkScope::~SinkScope()
    {
        gSink = m_previous;
    }
}
