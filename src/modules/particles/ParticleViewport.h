#pragma once

#include "particle_sim.h"

#include <QElapsedTimer>
#include <QHash>
#include <QMatrix4x4>
#include <QOpenGLFunctions_3_3_Core>
#include <QOpenGLShaderProgram>
#include <QOpenGLTexture>
#include <QOpenGLWidget>
#include <QStringList>
#include <QTimer>
#include <QVector3D>
#include <QVector4D>
#include <functional>
#include <memory>

namespace kpe
{
    // Live preview of a particle system: the engine simulation drawn over a
    // floor that the particle lights illuminate. HDR with a simple tonemap.
    class ParticleViewport : public QOpenGLWidget, protected QOpenGLFunctions_3_3_Core
    {
        Q_OBJECT
    public:
        explicit ParticleViewport(QWidget* parent = nullptr);
        ~ParticleViewport() override;

        // Restarts the simulation on `data` (which must stay alive).
        void setSystem(const ParticleSystemData* data);
        void restart();
        void setPlaying(bool playing);
        bool isPlaying() const { return m_playing; }
        void setSpeed(float speed) { m_speed = speed; }
        void setLooping(bool loop) { m_loop = loop; }
        void setMoving(bool moving);
        void setShowFloor(bool show);
        // 0 day (close to the engine's default ambient), 1 dusk, 2 night (particle lights stand out)
        void setLightingPreset(int preset);
        void setUserValue(const QString& name, float value);
        // content path -> absolute path, for materials and models
        void setResolver(std::function<QString(const QString&)> resolve);
        void clearAssetCache();
        void frameParticles();

        const QStringList& errors() const { return m_errors; }

    signals:
        void statsChanged(int particles, int lights, double simMs);
        void errorsChanged(const QStringList& errors);

    protected:
        void initializeGL() override;
        void resizeGL(int w, int h) override;
        void paintGL() override;
        void mousePressEvent(QMouseEvent* e) override;
        void mouseMoveEvent(QMouseEvent* e) override;
        void wheelEvent(QWheelEvent* e) override;
        void keyPressEvent(QKeyEvent* e) override;

    private:
        struct Material
        {
            std::unique_ptr<QOpenGLTexture> texture;
            QVector4D baseColor{ 1, 1, 1, 1 };
        };
        struct Mesh
        {
            GLuint vao = 0;
            GLuint vbo = 0;
            GLuint ibo = 0;
            int indexCount = 0;
        };

        void tick();
        void updateTransform(float time);
        Material* material(const std::string& path);
        Mesh* mesh(const std::string& path);
        void ensureTargets();
        void destroyGl();
        QVector3D eye() const;
        ParticleCameraBasis cameraBasis() const;

        const ParticleSystemData* m_data = nullptr;
        ParticleSystemInstance m_instance;
        QStringList m_errors;
        std::function<QString(const QString&)> m_resolve;
        QHash<QString, float> m_userValues;

        QTimer m_timer;
        QElapsedTimer m_clock;
        float m_time = 0.0f;
        float m_speed = 1.0f;
        float m_completeFor = 0.0f;
        bool m_playing = true;
        bool m_loop = true;
        bool m_moving = false;
        bool m_showFloor = true;
        int m_lightingPreset = 0;

        float m_yaw = 0.6f;
        float m_pitch = 0.35f;
        float m_distance = 7.0f;
        QVector3D m_target{ 0.0f, 1.0f, 0.0f };
        QPoint m_lastMouse;
        QMatrix4x4 m_viewProj;

        bool m_glReady = false;
        QOpenGLShaderProgram m_floorProgram;
        QOpenGLShaderProgram m_particleProgram;
        QOpenGLShaderProgram m_meshProgram;
        QOpenGLShaderProgram m_tonemapProgram;
        GLuint m_floorVao = 0;
        GLuint m_floorVbo = 0;
        GLuint m_particleVao = 0;
        GLuint m_particleVbo = 0;
        GLuint m_particleIbo = 0;
        GLuint m_emptyVao = 0;
        GLuint m_fbo = 0;
        GLuint m_colorTex = 0;
        GLuint m_depthRb = 0;
        GLuint m_whiteTex = 0;
        int m_targetW = 0;
        int m_targetH = 0;
        QHash<QString, std::shared_ptr<Material>> m_materials;
        QHash<QString, std::shared_ptr<Mesh>> m_meshes;
    };
}
