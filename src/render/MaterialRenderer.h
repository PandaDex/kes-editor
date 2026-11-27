#pragma once
#include "core/EditorContext.h"
#include "render/Camera.h"
#include "render/SceneLighting.h"
#include <QHash>
#include <QOpenGLBuffer>
#include <QOpenGLExtraFunctions>
#include <QOpenGLShaderProgram>
#include <QOpenGLTexture>
#include <QOpenGLVertexArrayObject>
#include <QSize>
#include <memory>

struct MaterialInfo;
class MaterialLibrary;
class MapDocument;

// Colour is the vertex colour only; material values are applied by the shader.
struct RenderVertex
{
    float x, y, z;
    float nx, ny, nz;
    float u, v;
    float r, g, b, a;
};

// Draws material geometry (brush faces with a material, kmodels) in the 3D view in one of the
// ShadingModes. Lit mirrors the engine's forward shading (kes_common.slang): lights, sky
// ambient/reflections, sun cascades and spot/point shadow slices, ACES tonemap.
class MaterialRenderer : protected QOpenGLExtraFunctions
{
public:
    struct Range
    {
        QString material; // "" = the engine's default material (white, lit)
        int first = 0;
        int count = 0;
    };

    bool initialize(QString *log); // context current
    void release();                // context current
    void releaseTextures();        // context current

    void setGeometry(const QVector<RenderVertex> &vertices, const QVector<Range> &ranges);
    bool isEmpty() const { return m_ranges.isEmpty(); }

    // Lit only, before the frame is drawn: shadow maps and the light/sky block.
    // Leaves `targetFbo` bound with a full viewport.
    void prepareLit(const MapDocument &doc, const Camera &cam, MaterialLibrary *materials, GLuint targetFbo, QSize targetSize);
    void drawSky(const Camera &cam);
    // Expects the caller's VAO bound; leaves blending/depth as it found them except the depth mask.
    void draw(ShadingMode mode, const Camera &cam, MaterialLibrary *materials);

private:
    enum Slot
    {
        BaseSlot,
        NormalSlot,
        MetallicRoughnessSlot,
        OcclusionSlot,
        EmissiveSlot,
        SlotCount
    };
    struct ShadowView
    {
        QMatrix4x4 viewProj;
        bool cascade = false;
        int slice = 0;
    };

    const MaterialInfo &material(MaterialLibrary *materials, const QString &path) const;
    QOpenGLTexture *texture(const QString &material, Slot slot, const QImage &image);
    void bindVertexAttributes(QOpenGLShaderProgram &program, bool full);
    void ensureShadowTargets();
    void renderShadows(const QVector<ShadowView> &views, MaterialLibrary *materials);
    void uploadEnvironment();

    QOpenGLShaderProgram m_mesh;
    QOpenGLShaderProgram m_depth;
    QOpenGLShaderProgram m_sky;
    QOpenGLBuffer m_vbo{QOpenGLBuffer::VertexBuffer};
    QOpenGLVertexArrayObject m_shadowVao; // the shadow pass runs before the caller binds its VAO
    QVector<Range> m_ranges;
    QHash<QString, std::shared_ptr<QOpenGLTexture>> m_textures; // null = slot unused
    bool m_ready = false;

    GLuint m_sceneBlock = 0;
    GLuint m_clusterLights = 0; // SSBO binding 1, point/spot lights
    GLuint m_clusterGrid = 0;   // SSBO binding 2, cluster headers + light indices
    GLuint m_cascadeMap = 0;
    GLuint m_localMap = 0;
    GLuint m_shadowFbo = 0;
    GLuint m_envTexture = 0;
    quint64 m_envUploaded = 0;
    LitEnvironment m_environment;
    LitSky m_skySettings;
};
