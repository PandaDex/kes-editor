#include "render/SceneLighting.h"
#include "core/MapDocument.h"
#include "core/ModelCache.h"

#include "engine_configuration.h"

#include <QRegularExpression>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace
{
constexpr float kPi = 3.14159265358979f;
constexpr float kDegToRad = kPi / 180.f;

float parseFloat(const MapObject &o, const char *key, float fallback)
{
    bool ok = false;
    const float v = objectValue(o, key).trimmed().toFloat(&ok);
    return ok ? v : fallback;
}

QVector3D parseVector(const MapObject &o, const char *key, const QVector3D &fallback)
{
    const QStringList parts = objectValue(o, key).split(QRegularExpression("[,\\s]+"), Qt::SkipEmptyParts);
    if (parts.size() < 3)
        return fallback;
    QVector3D out = fallback;
    for (int i = 0; i < 3; ++i)
    {
        bool ok = false;
        const float v = parts[i].toFloat(&ok);
        if (ok)
            out[i] = v;
    }
    return out;
}

bool parseBool(const QString &text, bool fallback)
{
    const QString v = text.trimmed().toLower();
    if (v == "1" || v == "true" || v == "yes")
        return true;
    if (v == "0" || v == "false" || v == "no")
        return false;
    return fallback;
}

// ConvertMapAngles then Vector3DirectionFromRotation, in engine space.
QVector3D engineDirection(const QVector3D &editorAngles)
{
    const float pitch = -editorAngles.x() * kDegToRad;
    const float yaw = (90.f - editorAngles.y()) * kDegToRad;
    return QVector3D(std::sin(yaw) * std::cos(pitch), std::sin(pitch), std::cos(yaw) * std::cos(pitch)).normalized();
}

bool isClass(const MapObject &o, const char *name)
{
    return o.classname.compare(QLatin1String(name), Qt::CaseInsensitive) == 0;
}

// ported from BuildEnvironment in render_common.cpp

struct Source
{
    bool sky = false;
    float top[3], horizon[3], bottom[3];
    float exponent = 1.f;
    float scale = 1.f;
    float flat[3] = {0.25f, 0.25f, 0.25f}; // SceneState::ambient

    void sample(const QVector3D &d, float out[3]) const
    {
        if (!sky)
        {
            std::memcpy(out, flat, sizeof(flat));
            return;
        }
        const float blend = std::pow(std::clamp(std::fabs(d.y()), 0.f, 1.f), std::max(exponent, 0.01f));
        const float *target = d.y() >= 0.f ? top : bottom;
        for (int i = 0; i < 3; ++i)
            out[i] = (horizon[i] + (target[i] - horizon[i]) * blend) * scale;
    }
};

QVector3D cubeDirection(int face, float u, float v)
{
    switch (face)
    {
    case 0: return QVector3D(1.f, -v, -u).normalized();
    case 1: return QVector3D(-1.f, -v, u).normalized();
    case 2: return QVector3D(u, 1.f, v).normalized();
    case 3: return QVector3D(u, -1.f, -v).normalized();
    case 4: return QVector3D(u, -v, 1.f).normalized();
    default: return QVector3D(-u, -v, -1.f).normalized();
    }
}

QVector3D importanceSampleGGX(float xi0, float xi1, float roughness, const QVector3D &n)
{
    const float a = roughness * roughness;
    const float phi = 2.f * kPi * xi0;
    const float cosTheta = std::sqrt((1.f - xi1) / (1.f + (a * a - 1.f) * xi1));
    const float sinTheta = std::sqrt(std::max(0.f, 1.f - cosTheta * cosTheta));
    const QVector3D h(sinTheta * std::cos(phi), sinTheta * std::sin(phi), cosTheta);
    const QVector3D up = std::fabs(n.z()) < 0.999f ? QVector3D(0, 0, 1) : QVector3D(1, 0, 0);
    const QVector3D tx = QVector3D::crossProduct(up, n).normalized();
    const QVector3D ty = QVector3D::crossProduct(n, tx);
    return (tx * h.x() + ty * h.y() + n * h.z()).normalized();
}

float radicalInverse(quint32 bits)
{
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return float(bits) * 2.3283064365386963e-10f;
}

quint64 hashBytes(quint64 hash, const void *data, size_t size)
{
    const auto *bytes = static_cast<const unsigned char *>(data);
    for (size_t i = 0; i < size; ++i)
        hash = (hash ^ bytes[i]) * 1099511628211ull;
    return hash;
}
}

LitScene gatherLitScene(const MapDocument &doc)
{
    LitScene scene;
    const float toEditor = 1.f / ENGINE_MAP_UNIT_SCALE;
    bool skyFound = false;
    for (const auto &entry : doc.objects())
    {
        const MapObject &o = entry.second;
        if (o.solid || !doc.isVisible(o))
            continue;
        if (isClass(o, "LightEntity"))
        {
            LitLight light;
            const QString type = objectValue(o, "Type").trimmed();
            light.type = type == "Directional" ? LitLight::Directional : type == "Spot" ? LitLight::Spot : LitLight::Point;
            const QVector3D color = parseVector(o, "Color", {1.f, 1.f, 1.f});
            const float intensity = parseFloat(o, "Intensity", 1.f);
            if (intensity <= 0.f || color.x() * 0.2126f + color.y() * 0.7152f + color.z() * 0.0722f <= 0.f)
                continue;
            light.radiance = color * (intensity * kPi);
            light.position = o.origin;
            light.direction = swapEngineAxes(engineDirection(o.angles));
            light.range = std::max(parseFloat(o, "Range", 10.f), 0.01f) * toEditor;
            light.spotAngle = parseFloat(o, "SpotAngle", 45.f);
            light.castShadows = parseBool(objectValue(o, "CastShadows"), light.type != LitLight::Point);
            scene.lights.push_back(light);
        }
        else if (isClass(o, "SkyboxEntity") && !skyFound)
        {
            LitSky &sky = scene.sky;
            sky.active = parseBool(objectValue(o, "Active", "1"), true);
            if (!sky.active)
                continue;
            skyFound = true;
            sky.top = parseVector(o, "TopColor", sky.top);
            sky.horizon = parseVector(o, "HorizonColor", sky.horizon);
            sky.bottom = parseVector(o, "BottomColor", sky.bottom);
            sky.sunColor = parseVector(o, "SunColor", sky.sunColor);
            sky.sunSize = std::max(parseFloat(o, "SunSize", sky.sunSize), 0.1f);
            sky.sunIntensity = std::max(parseFloat(o, "SunIntensity", sky.sunIntensity), 0.f);
            sky.horizonExponent = std::max(parseFloat(o, "HorizonExponent", sky.horizonExponent), 0.05f);
            sky.ambientIntensity = std::max(parseFloat(o, "AmbientIntensity", sky.ambientIntensity), 0.f);
            sky.sunDirection = engineDirection(o.angles);
        }
    }
    return scene;
}

void updateLitEnvironment(const LitSky &sky, LitEnvironment &env)
{
    Source source;
    source.sky = sky.active;
    for (int i = 0; i < 3; ++i)
    {
        source.top[i] = sky.top[i];
        source.horizon[i] = sky.horizon[i];
        source.bottom[i] = sky.bottom[i];
    }
    source.exponent = sky.horizonExponent;
    source.scale = std::max(sky.ambientIntensity, 0.f);

    quint64 key = 1469598103934665603ull;
    key = hashBytes(key, &source.sky, sizeof(source.sky));
    if (source.sky)
    {
        key = hashBytes(key, source.top, sizeof(source.top));
        key = hashBytes(key, source.horizon, sizeof(source.horizon));
        key = hashBytes(key, source.bottom, sizeof(source.bottom));
        key = hashBytes(key, &source.exponent, sizeof(source.exponent));
        key = hashBytes(key, &source.scale, sizeof(source.scale));
    }
    if (!env.table.isEmpty() && env.key == key)
        return;
    env.key = key;

    constexpr int kTable = LitEnvironment::kTableSize;
    constexpr int kSamples = 256;
    env.table.resize(LitEnvironment::kMipCount * kTable * 4);
    for (int mip = 0; mip < LitEnvironment::kMipCount; ++mip)
    {
        const float roughness = float(mip) / float(LitEnvironment::kMipCount - 1);
        for (int k = 0; k < kTable; ++k)
        {
            const float y = -1.f + 2.f * float(k) / float(kTable - 1);
            const QVector3D n = QVector3D(std::sqrt(std::max(0.f, 1.f - y * y)), y, 0.f).normalized();
            float sum[3] = {};
            if (mip == 0 || !source.sky)
            {
                source.sample(n, sum);
            }
            else
            {
                float weight = 0.f;
                for (int s = 0; s < kSamples; ++s)
                {
                    const QVector3D h = importanceSampleGGX((float(s) + 0.5f) / kSamples, radicalInverse(quint32(s)), roughness, n);
                    const QVector3D l = h * (2.f * QVector3D::dotProduct(n, h)) - n;
                    const float ndotl = QVector3D::dotProduct(n, l);
                    if (ndotl <= 0.f)
                        continue;
                    float radiance[3];
                    source.sample(l, radiance);
                    for (int c = 0; c < 3; ++c)
                        sum[c] += radiance[c] * ndotl;
                    weight += ndotl;
                }
                for (float &value : sum)
                    value /= std::max(weight, 1e-6f);
            }
            float *dst = &env.table[(mip * kTable + k) * 4];
            dst[0] = sum[0];
            dst[1] = sum[1];
            dst[2] = sum[2];
            dst[3] = 1.f;
        }
    }

    double coefficients[9][3] = {};
    double totalWeight = 0.0;
    constexpr int kShSize = 32;
    for (int face = 0; face < 6; ++face)
    {
        for (int py = 0; py < kShSize; ++py)
        {
            for (int px = 0; px < kShSize; ++px)
            {
                const float u = 2.f * (float(px) + 0.5f) / kShSize - 1.f;
                const float v = 2.f * (float(py) + 0.5f) / kShSize - 1.f;
                const QVector3D d = cubeDirection(face, u, v);
                const double weight = 1.0 / std::pow(1.0 + u * u + v * v, 1.5);
                float radiance[3];
                source.sample(d, radiance);
                const double x = d.x(), yy = d.y(), z = d.z();
                const double basis[9] = {0.282095,
                                         0.488603 * yy, 0.488603 * z, 0.488603 * x,
                                         1.092548 * x * yy, 1.092548 * yy * z, 0.315392 * (3.0 * z * z - 1.0),
                                         1.092548 * x * z, 0.546274 * (x * x - yy * yy)};
                for (int i = 0; i < 9; ++i)
                    for (int c = 0; c < 3; ++c)
                        coefficients[i][c] += radiance[c] * basis[i] * weight;
                totalWeight += weight;
            }
        }
    }
    const double normalize = 4.0 * kPi / totalWeight;
    const double band[9] = {1.0, 2.0 / 3.0, 2.0 / 3.0, 2.0 / 3.0, 0.25, 0.25, 0.25, 0.25, 0.25};
    for (int i = 0; i < 9; ++i)
    {
        for (int c = 0; c < 3; ++c)
            env.sh[i][c] = float(coefficients[i][c] * normalize * band[i]);
        env.sh[i][3] = 0.f;
    }
}
