#include "ParticleViewport.h"

#include "kmodel.h"
#include "mesh.h"
#include "modules/modelEditor/MaterialPreview.h"

#include <QFileInfo>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QtMath>
#include <algorithm>
#include <cmath>

namespace kpe
{
    namespace
    {
        constexpr int kMaxPreviewLights = 64;

        const char* kLightingGlsl = R"(
uniform int uLightCount;
uniform vec4 uLightPos[64];
uniform vec4 uLightColor[64];
uniform vec3 uAmbient;
uniform vec3 uSun;
vec3 lighting(vec3 p, vec3 n, bool wrap)
{
    vec3 sunDir = normalize(vec3(-0.4, 0.8, -0.3));
    float sun = wrap ? clamp((dot(n, sunDir) + 0.5) / 1.5, 0.0, 1.0) : max(dot(n, sunDir), 0.0);
    vec3 result = uAmbient + uSun * sun;
    for (int i = 0; i < uLightCount; ++i)
    {
        vec3 d = uLightPos[i].xyz - p;
        float dist = length(d);
        if (dist >= uLightPos[i].w)
            continue;
        float x = dist / uLightPos[i].w;
        float window = clamp(1.0 - x * x * x * x, 0.0, 1.0);
        float att = window * window / (1.0 + 4.0 * x * x);
        vec3 l = d / max(dist, 0.0001);
        float ndl = wrap ? clamp((dot(n, l) + 0.5) / 1.5, 0.0, 1.0) : max(dot(n, l), 0.0);
        result += uLightColor[i].rgb * ndl * att;
    }
    return result;
}
)";

        const char* kFloorVs = R"(#version 330 core
layout(location = 0) in vec3 aPos;
uniform mat4 uViewProj;
out vec3 vWorld;
void main() { vWorld = aPos; gl_Position = uViewProj * vec4(aPos, 1.0); }
)";

        const char* kFloorFsHead = R"(#version 330 core
in vec3 vWorld;
uniform vec3 uSkyColor;
uniform vec2 uFadeCenter;
uniform float uFadeRadius;
out vec4 fragColor;
)";
        const char* kFloorFsBody = R"(
void main()
{
    vec2 g = abs(fract(vWorld.xz - 0.5) - 0.5) / fwidth(vWorld.xz);
    float line = 1.0 - min(min(g.x, g.y), 1.0);
    vec2 g10 = abs(fract(vWorld.xz / 5.0 - 0.5) - 0.5) / fwidth(vWorld.xz / 5.0);
    float major = 1.0 - min(min(g10.x, g10.y), 1.0);
    vec3 albedo = vec3(0.32) + vec3(0.06) * line + vec3(0.1) * major;
    float fade = clamp(1.0 - length(vWorld.xz - uFadeCenter) / uFadeRadius, 0.0, 1.0);
    vec3 lit = albedo * lighting(vWorld, vec3(0.0, 1.0, 0.0), false);
    fragColor = vec4(mix(uSkyColor, lit, fade * fade * (3.0 - 2.0 * fade)), 1.0);
}
)";

        // Lit particles are lit per vertex: big overlapping smoke sprites would
        // otherwise run the light loop many times per pixel.
        const char* kParticleVsHead = R"(#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec2 aUv;
layout(location = 2) in vec4 aColor;
layout(location = 3) in vec3 aNormal;
uniform mat4 uViewProj;
uniform int uLit;
out vec2 vUv;
out vec4 vColor;
out vec3 vLight;
)";
        const char* kParticleVsBody = R"(
void main()
{
    vUv = aUv;
    vColor = aColor;
    vLight = uLit != 0 ? lighting(aPos, normalize(aNormal), true) : vec3(1.0);
    gl_Position = uViewProj * vec4(aPos, 1.0);
}
)";

        const char* kParticleFsHead = R"(#version 330 core
in vec2 vUv;
in vec4 vColor;
in vec3 vLight;
uniform sampler2D uTexture;
uniform vec4 uBaseColor;
uniform int uPremultiply;
uniform int uHasTexture;
uniform int uShape;
out vec4 fragColor;
)";
        const char* kParticleFsBody = R"(
void main()
{
    vec4 c = vColor * uBaseColor * texture(uTexture, vUv);
    if (uHasTexture == 0 && uShape == 2)
        c.a *= clamp(1.0 - abs(vUv.y * 2.0 - 1.0), 0.0, 1.0) * 1.5;
    else if (uHasTexture == 0 && uShape == 1)
    {
        float r = clamp(1.0 - length(vUv * 2.0 - 1.0), 0.0, 1.0);
        c.a *= r * r;
    }
    c.rgb *= vLight;
    c.rgb = max(c.rgb, vec3(0.0));
    c.a = clamp(c.a, 0.0, 1.0);
    if (uPremultiply != 0)
        c.rgb *= c.a;
    fragColor = c;
}
)";

        const char* kMeshVs = R"(#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
uniform mat4 uViewProj;
uniform mat4 uModel;
out vec3 vWorld;
out vec3 vNormal;
void main()
{
    vec4 w = uModel * vec4(aPos, 1.0);
    vWorld = w.xyz;
    vNormal = mat3(uModel) * aNormal;
    gl_Position = uViewProj * w;
}
)";

        const char* kMeshFsHead = R"(#version 330 core
in vec3 vWorld;
in vec3 vNormal;
uniform vec4 uColor;
out vec4 fragColor;
)";
        const char* kMeshFsBody = R"(
void main()
{
    fragColor = vec4(uColor.rgb * lighting(vWorld, normalize(vNormal), false), 1.0);
}
)";

        const char* kTonemapVs = R"(#version 330 core
out vec2 vUv;
void main()
{
    vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
    vUv = p;
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
)";

        const char* kTonemapFs = R"(#version 330 core
in vec2 vUv;
uniform sampler2D uScene;
out vec4 fragColor;
void main()
{
    vec3 c = texture(uScene, vUv).rgb;
    c = mix(max(c, vec3(0.0)), vec3(0.0), isnan(c));
    // ACES fit (Narkowicz), then sRGB
    c = clamp((c * (2.51 * c + 0.03)) / (c * (2.43 * c + 0.59) + 0.14), 0.0, 1.0);
    fragColor = vec4(pow(c, vec3(1.0 / 2.2)), 1.0);
}
)";

        bool build(QOpenGLShaderProgram& p, const QString& vs, const QString& fs)
        {
            return p.addShaderFromSourceCode(QOpenGLShader::Vertex, vs) && p.addShaderFromSourceCode(QOpenGLShader::Fragment, fs) && p.link();
        }

        QVector3D toQt(const Vector3& v) { return { v.x, v.y, v.z }; }
    }

    ParticleViewport::ParticleViewport(QWidget* parent)
        : QOpenGLWidget(parent)
    {
        setFocusPolicy(Qt::StrongFocus);
        setMinimumSize(200, 150);
        connect(&m_timer, &QTimer::timeout, this, &ParticleViewport::tick);
        m_timer.start(16);
        m_clock.start();
    }

    ParticleViewport::~ParticleViewport()
    {
        makeCurrent();
        destroyGl();
        doneCurrent();
    }

    void ParticleViewport::setResolver(std::function<QString(const QString&)> resolve)
    {
        m_resolve = std::move(resolve);
        clearAssetCache();
    }

    void ParticleViewport::clearAssetCache()
    {
        if (m_glReady)
        {
            makeCurrent();
            for (const auto& m : m_meshes)
            {
                glDeleteVertexArrays(1, &m->vao);
                glDeleteBuffers(1, &m->vbo);
                glDeleteBuffers(1, &m->ibo);
            }
            m_materials.clear();
            doneCurrent();
        }
        m_meshes.clear();
        m_materials.clear();
    }

    void ParticleViewport::setSystem(const ParticleSystemData* data)
    {
        m_data = data;
        restart();
    }

    void ParticleViewport::restart()
    {
        std::vector<std::string> errors;
        m_instance.Init(m_data, 0, &errors);
        QStringList list;
        for (const std::string& e : errors)
            list << QString::fromStdString(e);
        if (list != m_errors)
        {
            m_errors = list;
            emit errorsChanged(m_errors);
        }
        for (auto it = m_userValues.constBegin(); it != m_userValues.constEnd(); ++it)
            m_instance.SetUserFloat(it.key().toStdString(), it.value());
        m_time = 0.0f;
        m_completeFor = 0.0f;
        updateTransform(0.0f);
        m_instance.SetTransform(m_instance.GetPosition(), {}, { 1.0f, 1.0f, 1.0f }, true);
        m_instance.Activate(true);
        gatherLights();
        update();
    }

    void ParticleViewport::setPlaying(bool playing)
    {
        m_playing = playing;
        m_clock.restart();
    }

    void ParticleViewport::setMoving(bool moving)
    {
        m_moving = moving;
        updateTransform(m_time);
        m_instance.SetTransform(m_instance.GetPosition(), {}, { 1.0f, 1.0f, 1.0f }, true);
    }

    void ParticleViewport::setShowFloor(bool show)
    {
        m_showFloor = show;
        update();
    }

    void ParticleViewport::setLightingPreset(int preset)
    {
        m_lightingPreset = preset;
        update();
    }

    void ParticleViewport::setUserValue(const QString& name, float value)
    {
        m_userValues[name] = value;
        m_instance.SetUserFloat(name.toStdString(), value);
    }

    void ParticleViewport::updateTransform(float time)
    {
        Vector3 position;
        if (m_moving)
            position = { std::cos(time * 1.5f) * 2.5f, 0.0f, std::sin(time * 1.5f) * 2.5f };
        m_instance.SetTransform(position, {}, { 1.0f, 1.0f, 1.0f });
    }

    void ParticleViewport::tick()
    {
        const float real = std::min(float(m_clock.restart()) / 1000.0f, 0.1f);
        if (!m_playing || !m_data || !isVisible())
            return;
        const float dt = real * m_speed;
        if (dt <= 0.0f)
            return;
        m_time += dt;
        updateTransform(m_time);
        QElapsedTimer simClock;
        simClock.start();
        m_instance.Tick(dt);
        const double simMs = double(simClock.nsecsElapsed()) / 1e6;

        if (m_instance.IsComplete())
        {
            m_completeFor += dt;
            if (m_loop && m_completeFor > 0.5f)
                restart();
        }
        else
            m_completeFor = 0.0f;

        gatherLights();
        if (!m_statsClock.isValid() || m_statsClock.elapsed() > 250)
        {
            m_statsClock.start();
            emit statsChanged(int(m_instance.GetParticleCount()), int(m_lights.size()), simMs);
        }
        update();
    }

    void ParticleViewport::gatherLights()
    {
        m_lights.clear();
        if (!m_data)
            return;
        for (size_t e = 0; e < m_instance.GetEmitterCount(); ++e)
            for (const ParticleRendererData& r : m_instance.GetEmitterData(e).renderers)
                if (r.enabled && r.type == "Light")
                    m_instance.ExtractLights(e, r, m_lights);
    }

    QVector3D ParticleViewport::eye() const
    {
        const float cp = std::cos(m_pitch);
        return m_target + QVector3D(std::sin(m_yaw) * cp, std::sin(m_pitch), -std::cos(m_yaw) * cp) * m_distance;
    }

    ParticleCameraBasis ParticleViewport::cameraBasis() const
    {
        const QVector3D e = eye();
        ParticleCameraBasis b;
        b.position = { e.x(), e.y(), e.z() };
        b.forward = Vector3Normalize({ m_target.x() - e.x(), m_target.y() - e.y(), m_target.z() - e.z() });
        // Engine space is left-handed with Y up.
        b.right = Vector3Normalize(Vector3Cross({ 0.0f, 1.0f, 0.0f }, b.forward));
        b.up = Vector3Cross(b.forward, b.right);
        return b;
    }

    void ParticleViewport::frameParticles()
    {
        Vector3 mn, mx;
        if (m_instance.GetBounds(mn, mx))
        {
            m_target = (toQt(mn) + toQt(mx)) * 0.5f;
            m_distance = std::clamp((toQt(mx) - toQt(mn)).length() * 1.2f + 1.0f, 2.0f, 200.0f);
        }
        else
        {
            m_target = { 0.0f, 1.0f, 0.0f };
            m_distance = 7.0f;
        }
        update();
    }

    void ParticleViewport::initializeGL()
    {
        initializeOpenGLFunctions();
        const QString lightingHead = QString::fromLatin1(kLightingGlsl);
        const bool ok = build(m_floorProgram, kFloorVs, QString(kFloorFsHead) + lightingHead + kFloorFsBody) &&
            build(m_particleProgram, QString(kParticleVsHead) + lightingHead + kParticleVsBody, QString(kParticleFsHead) + kParticleFsBody) &&
            build(m_meshProgram, kMeshVs, QString(kMeshFsHead) + lightingHead + kMeshFsBody) &&
            build(m_tonemapProgram, kTonemapVs, kTonemapFs);
        if (!ok)
        {
            m_errors << "Preview shaders failed to compile.";
            emit errorsChanged(m_errors);
            return;
        }

        const float s = 1000.0f;
        const float floorVerts[] = { -s, 0, -s, s, 0, -s, s, 0, s, -s, 0, -s, s, 0, s, -s, 0, s };
        glGenVertexArrays(1, &m_floorVao);
        glGenBuffers(1, &m_floorVbo);
        glBindVertexArray(m_floorVao);
        glBindBuffer(GL_ARRAY_BUFFER, m_floorVbo);
        glBufferData(GL_ARRAY_BUFFER, sizeof(floorVerts), floorVerts, GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 12, nullptr);

        glGenVertexArrays(1, &m_particleVao);
        glGenBuffers(1, &m_particleVbo);
        glGenBuffers(1, &m_particleIbo);
        glBindVertexArray(m_particleVao);
        glBindBuffer(GL_ARRAY_BUFFER, m_particleVbo);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_particleIbo);
        const GLsizei stride = sizeof(ParticleVertex);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(offsetof(ParticleVertex, position)));
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(offsetof(ParticleVertex, uv)));
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(offsetof(ParticleVertex, color)));
        glEnableVertexAttribArray(3);
        glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(offsetof(ParticleVertex, normal)));
        glBindVertexArray(0);
        glGenVertexArrays(1, &m_emptyVao);

        const uint32_t white = 0xFFFFFFFFu;
        glGenTextures(1, &m_whiteTex);
        glBindTexture(GL_TEXTURE_2D, m_whiteTex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, &white);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        m_glReady = true;
        connect(context(), &QOpenGLContext::aboutToBeDestroyed, this, [this] { makeCurrent(); destroyGl(); doneCurrent(); });
    }

    void ParticleViewport::destroyGl()
    {
        if (!m_glReady)
            return;
        m_glReady = false;
        for (const auto& m : m_meshes)
        {
            glDeleteVertexArrays(1, &m->vao);
            glDeleteBuffers(1, &m->vbo);
            glDeleteBuffers(1, &m->ibo);
        }
        m_meshes.clear();
        m_materials.clear();
        glDeleteVertexArrays(1, &m_floorVao);
        glDeleteBuffers(1, &m_floorVbo);
        glDeleteVertexArrays(1, &m_particleVao);
        glDeleteBuffers(1, &m_particleVbo);
        glDeleteBuffers(1, &m_particleIbo);
        glDeleteVertexArrays(1, &m_emptyVao);
        glDeleteTextures(1, &m_whiteTex);
        glDeleteFramebuffers(1, &m_fbo);
        glDeleteTextures(1, &m_colorTex);
        glDeleteRenderbuffers(1, &m_depthRb);
        m_fbo = m_colorTex = m_depthRb = 0;
        m_targetW = m_targetH = 0;
    }

    void ParticleViewport::resizeGL(int, int)
    {
    }

    void ParticleViewport::ensureTargets()
    {
        // Logical pixels: on scaled displays this is far fewer pixels to shade; the tonemap upscales.
        const int w = std::max(1, width());
        const int h = std::max(1, height());
        if (m_fbo && w == m_targetW && h == m_targetH)
            return;
        if (!m_fbo)
        {
            glGenFramebuffers(1, &m_fbo);
            glGenTextures(1, &m_colorTex);
            glGenRenderbuffers(1, &m_depthRb);
        }
        glBindTexture(GL_TEXTURE_2D, m_colorTex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, w, h, 0, GL_RGBA, GL_HALF_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindRenderbuffer(GL_RENDERBUFFER, m_depthRb);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, w, h);
        glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_colorTex, 0);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, m_depthRb);
        m_targetW = w;
        m_targetH = h;
    }

    ParticleViewport::Material* ParticleViewport::material(const std::string& path)
    {
        const QString key = QString::fromStdString(path);
        auto it = m_materials.find(key);
        if (it != m_materials.end())
            return it->get();
        auto mat = std::make_shared<Material>();
        if (!key.isEmpty() && m_resolve)
        {
            kme::PreviewMaterial preview;
            if (kme::loadPreviewMaterial(m_resolve(key), m_resolve, preview))
            {
                mat->baseColor = preview.baseColor;
                if (!preview.texture.isNull())
                {
                    mat->texture = std::make_unique<QOpenGLTexture>(preview.texture.convertToFormat(QImage::Format_RGBA8888).mirrored(false, false));
                    mat->texture->setMinMagFilters(QOpenGLTexture::LinearMipMapLinear, QOpenGLTexture::Linear);
                    mat->texture->setWrapMode(QOpenGLTexture::ClampToEdge);
                }
            }
        }
        m_materials.insert(key, mat);
        return mat.get();
    }

    ParticleViewport::Mesh* ParticleViewport::mesh(const std::string& path)
    {
        const QString key = QString::fromStdString(path);
        auto it = m_meshes.find(key);
        if (it != m_meshes.end())
            return it->get();
        auto m = std::make_shared<Mesh>();
        MeshData data;
        KModelData model;
        const QString absolute = m_resolve && !key.isEmpty() ? m_resolve(key) : QString();
        if (absolute.isEmpty() || !LoadKModelFile(absolute.toUtf8().constData(), model) || !BuildMeshDataFromKModel(model, data))
        {
            // Unit cube stand-in for missing or non-kmodel meshes.
            const float n[6][3] = { { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 } };
            for (int f = 0; f < 6; ++f)
            {
                const Vector3 normal(n[f][0], n[f][1], n[f][2]);
                Vector3 a, b;
                const Vector3 helper = std::fabs(normal.y) < 0.9f ? Vector3(0, 1, 0) : Vector3(1, 0, 0);
                a = Vector3Normalize(Vector3Cross(helper, normal));
                b = Vector3Cross(normal, a);
                const uint32_t base = uint32_t(data.vertices.size());
                const float corners[4][2] = { { -1, -1 }, { 1, -1 }, { 1, 1 }, { -1, 1 } };
                for (const auto& c : corners)
                {
                    const Vector3 p = (normal + a * c[0] + b * c[1]) * 0.5f;
                    data.vertices.push_back({ p.x, p.y, p.z, normal.x, normal.y, normal.z, 0, 0, 1, 1, 1, 1 });
                }
                for (uint32_t i : { 0u, 1u, 2u, 0u, 2u, 3u })
                    data.indices.push_back(base + i);
            }
        }
        glGenVertexArrays(1, &m->vao);
        glGenBuffers(1, &m->vbo);
        glGenBuffers(1, &m->ibo);
        glBindVertexArray(m->vao);
        glBindBuffer(GL_ARRAY_BUFFER, m->vbo);
        glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(data.vertices.size() * sizeof(MeshVertex)), data.vertices.data(), GL_STATIC_DRAW);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m->ibo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, GLsizeiptr(data.indices.size() * sizeof(uint32_t)), data.indices.data(), GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(MeshVertex), reinterpret_cast<void*>(offsetof(MeshVertex, px)));
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(MeshVertex), reinterpret_cast<void*>(offsetof(MeshVertex, nx)));
        glBindVertexArray(0);
        m->indexCount = int(data.indices.size());
        m_meshes.insert(key, m);
        return m.get();
    }

    void ParticleViewport::paintGL()
    {
        if (!m_glReady)
            return;
        ensureTargets();
        const GLuint screenFbo = defaultFramebufferObject();
        glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
        glViewport(0, 0, m_targetW, m_targetH);
        const float clear[3][3] = { { 0.30f, 0.34f, 0.40f }, { 0.07f, 0.06f, 0.08f }, { 0.012f, 0.013f, 0.02f } };
        const int sky = std::clamp(m_lightingPreset, 0, 2);
        glClearColor(clear[sky][0], clear[sky][1], clear[sky][2], 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LEQUAL);
        glDisable(GL_CULL_FACE);

        // Engine space is left-handed: mirror Z into GL's right-handed view.
        QMatrix4x4 flip;
        flip.scale(1.0f, 1.0f, -1.0f);
        const QVector3D e = eye();
        QMatrix4x4 view;
        view.lookAt(flip.map(e), flip.map(m_target), QVector3D(0, 1, 0));
        view = view * flip;
        QMatrix4x4 proj;
        proj.perspective(60.0f, float(m_targetW) / float(std::max(m_targetH, 1)), 0.05f, 2000.0f);
        m_viewProj = proj * view;
        const ParticleCameraBasis basis = cameraBasis();

        std::vector<ParticleLightInstance> lights = m_lights;
        if (lights.size() > size_t(kMaxPreviewLights))
        {
            std::partial_sort(lights.begin(), lights.begin() + kMaxPreviewLights, lights.end(),
                [](const ParticleLightInstance& a, const ParticleLightInstance& b)
                { return a.color[0] + a.color[1] + a.color[2] > b.color[0] + b.color[1] + b.color[2]; });
            lights.resize(kMaxPreviewLights);
        }
        QVector4D lightPos[kMaxPreviewLights];
        QVector4D lightColor[kMaxPreviewLights];
        for (size_t i = 0; i < lights.size(); ++i)
        {
            lightPos[i] = QVector4D(lights[i].position[0], lights[i].position[1], lights[i].position[2], lights[i].radius);
            // The engine scales radiance by pi and its Lambert term divides by pi; both are left out here.
            lightColor[i] = QVector4D(lights[i].color[0], lights[i].color[1], lights[i].color[2], 0.0f);
        }
        static const QVector3D ambients[3] = { { 0.32f, 0.34f, 0.38f }, { 0.08f, 0.07f, 0.09f }, { 0.012f, 0.014f, 0.022f } };
        static const QVector3D suns[3] = { { 1.0f, 0.95f, 0.85f }, { 0.25f, 0.14f, 0.08f }, { 0.0f, 0.0f, 0.0f } };
        const int preset = std::clamp(m_lightingPreset, 0, 2);
        auto setLights = [&](QOpenGLShaderProgram& p)
        {
            p.setUniformValue("uAmbient", ambients[preset]);
            p.setUniformValue("uSun", suns[preset]);
            p.setUniformValue("uLightCount", int(lights.size()));
            if (!lights.empty())
            {
                p.setUniformValueArray("uLightPos", lightPos, int(lights.size()));
                p.setUniformValueArray("uLightColor", lightColor, int(lights.size()));
            }
        };

        if (m_showFloor)
        {
            m_floorProgram.bind();
            m_floorProgram.setUniformValue("uViewProj", m_viewProj);
            m_floorProgram.setUniformValue("uSkyColor", QVector3D(clear[sky][0], clear[sky][1], clear[sky][2]));
            // Follows the orbit target so the floor never ends inside the view, even framed on a big effect.
            m_floorProgram.setUniformValue("uFadeCenter", QVector2D(m_target.x(), m_target.z()));
            m_floorProgram.setUniformValue("uFadeRadius", std::max(15.0f, m_distance * 2.5f));
            setLights(m_floorProgram);
            glBindVertexArray(m_floorVao);
            glDrawArrays(GL_TRIANGLES, 0, 6);
        }

        if (m_data)
        {
            // Mesh particles (opaque).
            m_meshProgram.bind();
            m_meshProgram.setUniformValue("uViewProj", m_viewProj);
            setLights(m_meshProgram);
            std::vector<ParticleMeshInstance> meshes;
            for (size_t ei = 0; ei < m_instance.GetEmitterCount(); ++ei)
            {
                for (const ParticleRendererData& r : m_instance.GetEmitterData(ei).renderers)
                {
                    if (!r.enabled || r.type != "Mesh")
                        continue;
                    const ParticleParam* modelParam = r.Find("Model");
                    const ParticleParam* materialParam = r.Find("Material");
                    Mesh* m = mesh(modelParam ? modelParam->text : std::string());
                    const Material* mat = material(materialParam ? materialParam->text : std::string());
                    meshes.clear();
                    m_instance.ExtractMeshes(ei, r, meshes);
                    glBindVertexArray(m->vao);
                    for (const ParticleMeshInstance& inst : meshes)
                    {
                        const QMatrix4x4 model = QMatrix4x4(inst.world).transposed();
                        m_meshProgram.setUniformValue("uModel", model);
                        m_meshProgram.setUniformValue("uColor", QVector4D(inst.color[0], inst.color[1], inst.color[2], 1.0f) * mat->baseColor);
                        glDrawElements(GL_TRIANGLES, m->indexCount, GL_UNSIGNED_INT, nullptr);
                    }
                }
            }

            // Sprites and ribbons, back to front by emitter.
            struct Batch
            {
                std::vector<ParticleVertex> vertices;
                std::vector<uint32_t> indices;
                ParticleBlend blend;
                bool lit;
                int shape;
                int sortOrder;
                std::string material;
                float distance;
            };
            std::vector<Batch> batches;
            for (size_t ei = 0; ei < m_instance.GetEmitterCount(); ++ei)
            {
                const ParticleEmitterState& state = m_instance.GetEmitter(ei);
                const Vector3 center = (state.boundsMin + state.boundsMax) * 0.5f;
                const float distance = Vector3LengthSq(center - basis.position);
                for (const ParticleRendererData& r : m_instance.GetEmitterData(ei).renderers)
                {
                    if (!r.enabled)
                        continue;
                    Batch b;
                    b.distance = distance;
                    if (r.type == "Sprite")
                    {
                        const ParticleSpriteSettings settings = ReadParticleSpriteSettings(r);
                        std::vector<ParticleSpriteInstance> sprites;
                        m_instance.ExtractSprites(ei, r, sprites);
                        BuildParticleSpriteGeometry(settings, sprites, basis, b.vertices, b.indices);
                        b.blend = settings.blend;
                        b.lit = settings.lit;
                        b.shape = settings.columns * settings.rows > 1 ? 0 : 1;
                        b.sortOrder = settings.sortOrder;
                        b.material = settings.material;
                    }
                    else if (r.type == "Ribbon")
                    {
                        const ParticleRibbonSettings settings = ReadParticleRibbonSettings(r);
                        std::vector<ParticleRibbonPoint> points;
                        m_instance.ExtractRibbon(ei, r, points);
                        BuildParticleRibbonGeometry(settings, points, basis, b.vertices, b.indices);
                        b.blend = settings.blend;
                        b.lit = settings.lit;
                        b.shape = 2;
                        b.sortOrder = settings.sortOrder;
                        b.material = settings.material;
                    }
                    if (!b.indices.empty())
                        batches.push_back(std::move(b));
                }
            }
            std::stable_sort(batches.begin(), batches.end(), [](const Batch& a, const Batch& b)
                { return ParticleBatchDrawsBefore(a.sortOrder, a.blend, a.distance, b.sortOrder, b.blend, b.distance); });

            m_particleProgram.bind();
            m_particleProgram.setUniformValue("uViewProj", m_viewProj);
            m_particleProgram.setUniformValue("uTexture", 0);
            setLights(m_particleProgram);
            glDepthMask(GL_FALSE);
            glEnable(GL_BLEND);
            glBindVertexArray(m_particleVao);
            glActiveTexture(GL_TEXTURE0);
            for (const Batch& b : batches)
            {
                const Material* mat = material(b.material);
                if (mat->texture)
                    mat->texture->bind(0);
                else
                    glBindTexture(GL_TEXTURE_2D, m_whiteTex);
                m_particleProgram.setUniformValue("uBaseColor", mat->baseColor);
                m_particleProgram.setUniformValue("uLit", b.lit ? 1 : 0);
                m_particleProgram.setUniformValue("uHasTexture", mat->texture ? 1 : 0);
                m_particleProgram.setUniformValue("uShape", b.shape);
                m_particleProgram.setUniformValue("uPremultiply", b.blend == ParticleBlend::Alpha ? 0 : 1);
                if (b.blend == ParticleBlend::Additive)
                    glBlendFuncSeparate(GL_ONE, GL_ONE, GL_ZERO, GL_ONE);
                else if (b.blend == ParticleBlend::Alpha)
                    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
                else
                    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
                glBindBuffer(GL_ARRAY_BUFFER, m_particleVbo);
                glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(b.vertices.size() * sizeof(ParticleVertex)), b.vertices.data(), GL_STREAM_DRAW);
                glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_particleIbo);
                glBufferData(GL_ELEMENT_ARRAY_BUFFER, GLsizeiptr(b.indices.size() * sizeof(uint32_t)), b.indices.data(), GL_STREAM_DRAW);
                glDrawElements(GL_TRIANGLES, GLsizei(b.indices.size()), GL_UNSIGNED_INT, nullptr);
            }
            glDisable(GL_BLEND);
            glDepthMask(GL_TRUE);
        }

        glBindFramebuffer(GL_FRAMEBUFFER, screenFbo);
        glViewport(0, 0, std::max(1, int(width() * devicePixelRatioF())), std::max(1, int(height() * devicePixelRatioF())));
        glDisable(GL_DEPTH_TEST);
        m_tonemapProgram.bind();
        m_tonemapProgram.setUniformValue("uScene", 0);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, m_colorTex);
        glBindVertexArray(m_emptyVao);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glBindVertexArray(0);
    }

    void ParticleViewport::mousePressEvent(QMouseEvent* e)
    {
        m_lastMouse = e->position().toPoint();
        setFocus();
    }

    void ParticleViewport::mouseMoveEvent(QMouseEvent* e)
    {
        const QPoint delta = e->position().toPoint() - m_lastMouse;
        m_lastMouse = e->position().toPoint();
        if (e->buttons() & Qt::LeftButton)
        {
            m_yaw -= float(delta.x()) * 0.008f;
            m_pitch = std::clamp(m_pitch + float(delta.y()) * 0.008f, -1.5f, 1.5f);
        }
        else if (e->buttons() & (Qt::RightButton | Qt::MiddleButton))
        {
            const ParticleCameraBasis b = cameraBasis();
            const float scale = m_distance * 0.002f;
            m_target += toQt(b.right) * (-float(delta.x()) * scale) + toQt(b.up) * (float(delta.y()) * scale);
        }
        update();
    }

    void ParticleViewport::wheelEvent(QWheelEvent* e)
    {
        m_distance = std::clamp(m_distance * std::pow(0.9f, float(e->angleDelta().y()) / 120.0f), 0.5f, 300.0f);
        update();
    }

    void ParticleViewport::keyPressEvent(QKeyEvent* e)
    {
        if (e->key() == Qt::Key_F)
            frameParticles();
        else if (e->key() == Qt::Key_Space)
            setPlaying(!m_playing);
        else
            QOpenGLWidget::keyPressEvent(e);
    }
}
