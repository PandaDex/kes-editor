#pragma once
#include <QVector3D>
#include <QVector>
#include <array>

class MapDocument;

// Editor-space (Z up, inches) copies of what the engine builds from a map's
// LightEntity and SkyboxEntity objects. Directions and colours follow
// ent_light.cpp, ent_skybox.cpp and render_common.cpp (PackLight).
struct LitLight
{
    enum Type
    {
        Directional = 0,
        Point = 1,
        Spot = 2
    };
    int type = Point;
    QVector3D position;
    QVector3D direction; // the way the light travels
    QVector3D radiance;  // colour * intensity * pi
    float range = 0.f;   // editor units
    float spotAngle = 45.f;
    bool castShadows = false;
};

struct LitSky
{
    bool active = false;
    QVector3D top{0.09f, 0.16f, 0.29f};
    QVector3D horizon{0.52f, 0.62f, 0.78f};
    QVector3D bottom{0.03f, 0.03f, 0.05f};
    QVector3D sunColor{1.f, 0.94f, 0.82f};
    QVector3D sunDirection{-0.25f, -0.65f, 0.72f}; // engine space (Y up)
    float sunSize = 2.5f;
    float sunIntensity = 1.15f;
    float horizonExponent = 1.6f;
    float ambientIntensity = 1.f;
};

// Sky lighting, engine space: SH9 irradiance / pi and a GGX-prefiltered radiance table
// over direction.y (the gradient sky is symmetric about the vertical axis).
struct LitEnvironment
{
    static constexpr int kTableSize = 256;
    static constexpr int kMipCount = 6;
    float sh[9][4] = {};
    QVector<float> table; // kMipCount rows of kTableSize RGBA texels
    quint64 key = 0;
};

struct LitScene
{
    QVector<LitLight> lights;
    LitSky sky;
};

LitScene gatherLitScene(const MapDocument &doc);
// Rebuilds `env` only when the sky changed since it was last built.
void updateLitEnvironment(const LitSky &sky, LitEnvironment &env);
// Engine space (Y up) <-> editor space (Z up); the same swap in both directions.
inline QVector3D swapEngineAxes(const QVector3D &v) { return {v.x(), v.z(), v.y()}; }
