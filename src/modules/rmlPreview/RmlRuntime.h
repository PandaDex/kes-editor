#pragma once

#include <QString>
#include <QStringList>
#include <functional>

namespace Rml
{
    class Context;
}

class PoCatalog;

namespace rmlp
{
    // Receives what RmlUi does on behalf of one preview while it is the active sink.
    class RmlSink
    {
    public:
        virtual ~RmlSink() = default;
        virtual void rmlLog(bool error, const QString& message) = 0;
        // Absolute path of every file RmlUi read (documents, style sheets, templates, images).
        virtual void rmlFileRead(const QString& path) = 0;
        virtual const PoCatalog* rmlCatalog() const = 0;
        // Search folders for paths that are not absolute, most important first.
        virtual QStringList rmlSearchRoots() const = 0;
        virtual void rmlAction(const QString& name, const QString& args) = 0;
    };

    // RmlUi is a process-wide singleton: the first preview initialises it, the last one shuts it
    // down. Every call into RmlUi should happen inside a SinkScope so logs, file reads and
    // translations reach the right preview. Needs a current GL context when RmlUi may touch the
    // GPU; ensureCurrentContext() provides one when no preview's context is current.
    namespace runtime
    {
        bool acquire();
        void release();

        void ensureCurrentContext();
        void loadFonts(const QStringList& absolutePaths, const QStringList& fallbackAbsolutePaths);
        void registerContext(Rml::Context* context, RmlSink* sink);
        void unregisterContext(Rml::Context* context);

        // GL drawing state for the next Context::Render(). Coordinates in framebuffer pixels,
        // viewport y from the top.
        void beginFrame(unsigned vao, int framebufferHeight, int viewportX, int viewportY, float scale,
            int contextWidth, int contextHeight);
        void endFrame();
    }

    class SinkScope
    {
    public:
        explicit SinkScope(RmlSink* sink);
        ~SinkScope();
        SinkScope(const SinkScope&) = delete;
        SinkScope& operator=(const SinkScope&) = delete;

    private:
        RmlSink* m_previous;
    };
}
