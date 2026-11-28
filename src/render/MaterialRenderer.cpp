#include "render/MaterialRenderer.h"
#include "core/MapDocument.h"
#include "core/MaterialLibrary.h"

#include "engine_configuration.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

namespace
{
constexpr float kPi = 3.14159265358979f;
constexpr int kMaxDirectionalLights = 4;
// Clustered point/spot lights, as render_common.h (froxel grid binned on the CPU; the grid
// starts with one header per cluster, (first index << 8) | count, the light indices follow).
constexpr int kClusterX = 16;
constexpr int kClusterY = 9;
constexpr int kClusterZ = 24;
constexpr int kClusterCount = kClusterX * kClusterY * kClusterZ;
constexpr int kMaxClusterLights = 4096;
constexpr int kMaxClusterIndices = 256 * 1024;
constexpr quint32 kMaxLightsPerCluster = 255;
constexpr float kClusterFarMetres = 500.f;
constexpr int kCascadeCount = 4;       // ShadowQuality::High, the game's default
constexpr int kCascadeResolution = 2048;
constexpr int kLocalSlices = 12;
constexpr int kLocalResolution = 1024;
constexpr int kMaxLocalSlices = 16;
constexpr float kShadowDistanceMetres = 60.f;
constexpr float kCameraNearMetres = 0.1f;

enum MaterialFlag
{
    FlagLit = 1,
    FlagDoubleSided = 4,
    FlagHasBase = 8,
    FlagHasNormal = 16,
    FlagHasMetallicRoughness = 32,
    FlagHasOcclusion = 64,
    FlagHasEmissive = 128
};

// std140 / std430: vec4/mat4 members only, so the C++ layout matches without padding rules.
struct GpuLight
{
    float position[4];  // w = range
    float color[4];
    float direction[4]; // w = spot outer cos
    float params[4];    // x = type, y = spot inner cos, z = shadow (dir 1, local first slice, -1 none)
};

struct SceneBlock
{
    float cameraPosition[4];
    float cameraForward[4];
    float ambientSH[9][4];
    float environmentParams[4]; // x = mip count, y = intensity
    float lightCounts[4];       // x = directional light count
    GpuLight lights[kMaxDirectionalLights];
    float cascadeViewProj[4][16];
    float cascadeSplits[4];
    float cascadeTexelSize[4];
    float shadowParams[4]; // x = cascades, y = 1/cascade res, z = 1/local res, w = enabled
    float localViewProj[kMaxLocalSlices][16];
    float clusterParams[4]; // xyz = grid size, w = cluster light count
    float clusterDepth[4];  // x = near, y = slices / log(far / near)
    float clusterRight[4];  // xyz = camera right, w = x projection scale
    float clusterUp[4];     // xyz = camera up, w = y projection scale
};

const char *kMeshVertex = R"(#version 430 core
layout(location=0) in vec3 aPos;
layout(location=1) in vec3 aNormal;
layout(location=2) in vec2 aUv;
layout(location=3) in vec4 aCol;
uniform mat4 uViewProj;
out vec3 vPos; out vec3 vNormal; out vec2 vUv; out vec4 vCol;
void main()
{
    vPos = aPos; vNormal = aNormal; vUv = aUv; vCol = aCol;
    gl_Position = uViewProj * vec4(aPos, 1.0);
}
)";

// Shared by the mesh and sky shaders.
const char *kColourFunctions = R"(
vec3 srgbToLinear(vec3 c) { return mix(c / 12.92, pow((c + 0.055) / 1.055, vec3(2.4)), step(vec3(0.04045), c)); }
vec3 linearToSrgb(vec3 c)
{
    c = clamp(c, 0.0, 1.0);
    return mix(c * 12.92, 1.055 * pow(c, vec3(1.0 / 2.4)) - 0.055, step(vec3(0.0031308), c));
}
// blit.slang: Hill's ACES fit. Rows as written there, applied as row-vector products.
vec3 acesFitted(vec3 color)
{
    const mat3 kIn = mat3(0.59719, 0.35458, 0.04823, 0.07600, 0.90834, 0.01566, 0.02840, 0.13383, 0.83777);
    const mat3 kOut = mat3(1.60475, -0.53108, -0.07367, -0.10208, 1.10813, -0.00605, -0.00327, -0.07276, 1.07602);
    color = color * kIn;
    vec3 a = color * (color + 0.0245786) - 0.000090537;
    vec3 b = color * (0.983729 * color + 0.4329510) + 0.238081;
    return clamp((a / b) * kOut, 0.0, 1.0);
}
)";

// Lighting is done in editor space (Z up, inches); the SH, the reflection table and the
// derivative normal mapping use engine-space (Y up) vectors so they match the game exactly.
const char *kMeshFragment = R"(
in vec3 vPos; in vec3 vNormal; in vec2 vUv; in vec4 vCol;
out vec4 oCol;
uniform int uMode;
uniform vec4 uBaseColor;
uniform vec4 uEmissive; // a = alpha cutoff
uniform vec4 uPbr;      // metallic, roughness, normal scale, occlusion strength
uniform int uFlags;
uniform sampler2D uBaseTex;
uniform sampler2D uNormalTex;
uniform sampler2D uMetallicRoughnessTex;
uniform sampler2D uOcclusionTex;
uniform sampler2D uEmissiveTex;
uniform sampler2DArrayShadow uCascadeMap;
uniform sampler2DArrayShadow uLocalMap;
uniform sampler2D uEnvTable;

struct Light
{
    vec4 position;  // w = range
    vec4 color;
    vec4 direction; // w = spot outer cos
    vec4 params;    // x = type, y = spot inner cos, z = shadow
};

layout(std140) uniform SceneBlock
{
    vec4 uCameraPos;
    vec4 uCameraForward;
    vec4 uAmbientSH[9];
    vec4 uEnvParams;
    vec4 uLightCounts;
    Light uLights[4];
    mat4 uCascadeViewProj[4];
    vec4 uCascadeSplits;
    vec4 uCascadeTexel;
    vec4 uShadowParams;
    mat4 uLocalViewProj[16];
    vec4 uClusterParams;
    vec4 uClusterDepth;
    vec4 uClusterRight;
    vec4 uClusterUp;
};

layout(std430, binding = 1) readonly buffer ClusterLights { Light uClusterLights[]; };
layout(std430, binding = 2) readonly buffer ClusterGrid { uint uClusterGrid[]; };

// Same view-space math as the CPU binning in MaterialRenderer::prepareLit.
uint clusterHeader(vec3 worldPos)
{
    ivec3 dims = ivec3(uClusterParams.xyz);
    vec3 d = worldPos - uCameraPos.xyz;
    float viewZ = max(dot(d, uCameraForward.xyz), uClusterDepth.x);
    float nx = dot(d, uClusterRight.xyz) * uClusterRight.w / viewZ;
    float ny = dot(d, uClusterUp.xyz) * uClusterUp.w / viewZ;
    int x = clamp(int(floor((nx * 0.5 + 0.5) * dims.x)), 0, dims.x - 1);
    int y = clamp(int(floor((ny * 0.5 + 0.5) * dims.y)), 0, dims.y - 1);
    int z = clamp(int(floor(log(viewZ / uClusterDepth.x) * uClusterDepth.y)), 0, dims.z - 1);
    return uClusterGrid[(z * dims.y + y) * dims.x + x];
}
uint clusterLightIndex(uint header, uint i)
{
    ivec3 dims = ivec3(uClusterParams.xyz);
    return uClusterGrid[uint(dims.x * dims.y * dims.z) + (header >> 8) + i];
}

const float kPi = 3.14159265;
bool has(int flag) { return (uFlags & flag) != 0; }

float distributionGGX(float NoH, float a)
{
    float a2 = a * a;
    float d = (NoH * a2 - NoH) * NoH + 1.0;
    return a2 / (kPi * d * d + 1e-7);
}
float visibilitySmithGGX(float NoV, float NoL, float a)
{
    float a2 = a * a;
    float v = NoL * sqrt(NoV * NoV * (1.0 - a2) + a2);
    float l = NoV * sqrt(NoL * NoL * (1.0 - a2) + a2);
    return 0.5 / (v + l + 1e-7);
}
vec3 fresnelSchlick(vec3 f0, float VoH) { return f0 + (1.0 - f0) * pow(1.0 - VoH, 5.0); }
vec3 environmentBRDF(vec3 f0, float roughness, float NoV)
{
    const vec4 c0 = vec4(-1.0, -0.0275, -0.572, 0.022);
    const vec4 c1 = vec4(1.0, 0.0425, 1.04, -0.04);
    vec4 r = roughness * c0 + c1;
    float a004 = min(r.x * r.x, exp2(-9.28 * NoV)) * r.x + r.y;
    vec2 ab = vec2(-1.04, 1.04) * a004 + r.zw;
    return f0 * ab.x + ab.y;
}
vec3 ambientIrradiance(vec3 n) // engine space
{
    vec3 result = uAmbientSH[0].rgb * 0.282095;
    result += uAmbientSH[1].rgb * (0.488603 * n.y);
    result += uAmbientSH[2].rgb * (0.488603 * n.z);
    result += uAmbientSH[3].rgb * (0.488603 * n.x);
    result += uAmbientSH[4].rgb * (1.092548 * n.x * n.y);
    result += uAmbientSH[5].rgb * (1.092548 * n.y * n.z);
    result += uAmbientSH[6].rgb * (0.315392 * (3.0 * n.z * n.z - 1.0));
    result += uAmbientSH[7].rgb * (1.092548 * n.x * n.z);
    result += uAmbientSH[8].rgb * (0.546274 * (n.x * n.x - n.y * n.y));
    return max(result, 0.0);
}
vec3 prefilteredRadiance(float y, float roughness) // engine-space direction.y
{
    float u = ((clamp(y, -1.0, 1.0) + 1.0) * 0.5 * 255.0 + 0.5) / 256.0;
    float v = (roughness * (uEnvParams.x - 1.0) + 0.5) / uEnvParams.x;
    return texture(uEnvTable, vec2(u, v)).rgb;
}

float shadowPCF(sampler2DArrayShadow map, vec2 uv, float slice, float depth, float texel)
{
    float sum = 0.0;
    for (int y = -1; y <= 1; y++)
        for (int x = -1; x <= 1; x++)
            sum += texture(map, vec4(uv + vec2(x, y) * texel, slice, depth));
    return sum / 9.0;
}
float cascadeShadow(vec3 worldPos, vec3 geometricNormal)
{
    int count = int(uShadowParams.x);
    float viewDepth = dot(worldPos - uCameraPos.xyz, uCameraForward.xyz);
    int cascade = count;
    for (int i = 0; i < count; i++)
    {
        if (viewDepth < uCascadeSplits[i])
        {
            cascade = i;
            break;
        }
    }
    if (cascade >= count)
        return 1.0;
    vec3 p = worldPos + geometricNormal * (uCascadeTexel[cascade] * 1.5);
    vec4 clip = uCascadeViewProj[cascade] * vec4(p, 1.0);
    vec3 ndc = clip.xyz / clip.w;
    vec2 uv = ndc.xy * 0.5 + 0.5;
    float depth = ndc.z * 0.5 + 0.5;
    if (any(lessThan(uv, vec2(0.0))) || any(greaterThan(uv, vec2(1.0))) || depth > 1.0)
        return 1.0;
    return shadowPCF(uCascadeMap, uv, float(cascade), depth - 0.0005, uShadowParams.y);
}
float localShadow(Light light, vec3 worldPos, vec3 geometricNormal)
{
    float slice = light.params.z;
    vec3 fromLight = worldPos - light.position.xyz;
    if (light.params.x < 1.5)
    {
        vec3 a = abs(fromLight);
        slice += a.x >= a.y && a.x >= a.z ? (fromLight.x >= 0.0 ? 0.0 : 1.0)
               : a.y >= a.z ? (fromLight.y >= 0.0 ? 2.0 : 3.0)
               : (fromLight.z >= 0.0 ? 4.0 : 5.0);
    }
    float distanceToLight = length(fromLight);
    vec3 p = worldPos + geometricNormal * (distanceToLight * uShadowParams.z * 2.0);
    vec4 clip = uLocalViewProj[int(slice)] * vec4(p, 1.0);
    if (clip.w <= 0.0001)
        return 1.0;
    vec3 ndc = clip.xyz / clip.w;
    vec2 uv = ndc.xy * 0.5 + 0.5;
    float depth = ndc.z * 0.5 + 0.5;
    if (any(lessThan(uv, vec2(0.0))) || any(greaterThan(uv, vec2(1.0))) || depth > 1.0)
        return 1.0;
    return shadowPCF(uLocalMap, uv, slice, depth - 0.00005, uShadowParams.z);
}

// KesDirectLight: GGX specular + Lambert diffuse with the light's shadow.
vec3 directLight(Light light, vec3 worldPos, vec3 geometricNormal, vec3 n, vec3 v, float NoV, float a, vec3 f0,
                 vec3 diffuseColor)
{
    vec3 l;
    float attenuation = 1.0;
    if (light.params.x < 0.5)
    {
        l = -normalize(light.direction.xyz);
    }
    else
    {
        vec3 toLight = light.position.xyz - worldPos;
        float dist = length(toLight);
        if (dist >= light.position.w)
            return vec3(0.0);
        l = toLight / max(dist, 0.0001);
        float x = dist / light.position.w;
        float window = clamp(1.0 - x * x * x * x, 0.0, 1.0);
        attenuation = window * window / (1.0 + 4.0 * x * x);
        if (light.params.x > 1.5)
            attenuation *= smoothstep(light.direction.w, light.params.y, dot(-l, normalize(light.direction.xyz)));
    }
    float NoL = dot(n, l);
    if (NoL <= 0.0 || attenuation <= 0.0)
        return vec3(0.0);
    if (uShadowParams.w > 0.5 && light.params.z >= 0.0)
        attenuation *= light.params.x < 0.5 ? cascadeShadow(worldPos, geometricNormal) : localShadow(light, worldPos, geometricNormal);
    vec3 h = normalize(v + l);
    float NoH = clamp(dot(n, h), 0.0, 1.0);
    float VoH = clamp(dot(v, h), 0.0, 1.0);
    vec3 f = fresnelSchlick(f0, VoH);
    vec3 specular = distributionGGX(NoH, a) * visibilitySmithGGX(NoV, NoL, a) * f;
    vec3 diffuse = (1.0 - f) * diffuseColor / kPi;
    return (diffuse + specular) * light.color.rgb * (NoL * attenuation);
}

// KesPerturbNormal in engine space with D3D's screen-space y (ddy points down).
vec3 perturbNormal(vec3 n, vec3 worldPos, vec2 uv, vec3 tangentNormal)
{
    n = n.xzy;
    worldPos = worldPos.xzy;
    vec3 dp1 = dFdx(worldPos);
    vec3 dp2 = -dFdy(worldPos);
    vec2 duv1 = dFdx(uv);
    vec2 duv2 = -dFdy(uv);
    vec3 dp2perp = cross(dp2, n);
    vec3 dp1perp = cross(n, dp1);
    vec3 t = dp2perp * duv1.x + dp1perp * duv2.x;
    vec3 b = dp2perp * duv1.y + dp1perp * duv2.y;
    float invMax = inversesqrt(max(max(dot(t, t), dot(b, b)), 1e-12));
    return normalize(t * (tangentNormal.x * invMax) + b * (tangentNormal.y * invMax) + n * tangentNormal.z).xzy;
}

void main()
{
    vec4 tex = has(8) ? texture(uBaseTex, vUv) : vec4(1.0);
    vec3 n0 = normalize(vNormal);
    if (uMode == 0)
    {
        float shade = 0.42 + 0.58 * (0.5 + 0.5 * dot(n0, normalize(vec3(0.35, 0.55, 0.76))));
        oCol = vec4(tex.rgb * clamp(uBaseColor.rgb, 0.0, 1.0) * vCol.rgb * shade, tex.a * vCol.a);
        return;
    }

    vec4 base = uBaseColor * vCol * vec4(srgbToLinear(tex.rgb), tex.a);
    if (uEmissive.a > 0.0 && base.a < uEmissive.a)
        discard;
    vec3 emissive = uEmissive.rgb;
    if (has(128))
        emissive *= srgbToLinear(texture(uEmissiveTex, vUv).rgb);
    if (uMode == 1)
    {
        oCol = vec4(linearToSrgb(base.rgb + emissive), base.a);
        return;
    }

    vec3 v = normalize(uCameraPos.xyz - vPos);
    vec3 geometricNormal = has(4) && dot(n0, v) < 0.0 ? -n0 : n0;
    vec3 color = base.rgb + emissive;
    if (has(1))
    {
        vec3 n = geometricNormal;
        float metallic = uPbr.x;
        float roughness = uPbr.y;
        float occlusion = 1.0;
        if (has(16))
        {
            vec2 nxy = texture(uNormalTex, vUv).xy * 2.0 - 1.0;
            vec3 tangentNormal = vec3(nxy, sqrt(clamp(1.0 - dot(nxy, nxy), 0.0, 1.0)));
            tangentNormal.xy *= uPbr.z;
            n = perturbNormal(geometricNormal, vPos, vUv, normalize(tangentNormal));
        }
        if (has(32))
        {
            vec4 mr = texture(uMetallicRoughnessTex, vUv);
            roughness *= mr.g;
            metallic *= mr.b;
        }
        if (has(64))
            occlusion = mix(1.0, texture(uOcclusionTex, vUv).r, uPbr.w);

        metallic = clamp(metallic, 0.0, 1.0);
        roughness = clamp(roughness, 0.03, 1.0);
        float NoV = max(dot(n, v), 1e-4);
        float a = roughness * roughness;
        vec3 f0 = mix(vec3(0.04), base.rgb, metallic);
        vec3 diffuseColor = base.rgb * (1.0 - metallic);

        vec3 direct = vec3(0.0);
        int lightCount = int(uLightCounts.x);
        for (int i = 0; i < lightCount; i++)
            direct += directLight(uLights[i], vPos, geometricNormal, n, v, NoV, a, f0, diffuseColor);
        if (uClusterParams.w > 0.5)
        {
            uint header = clusterHeader(vPos);
            uint count = header & 0xFFu;
            for (uint ci = 0u; ci < count; ci++)
                direct += directLight(uClusterLights[clusterLightIndex(header, ci)], vPos, geometricNormal, n, v, NoV, a, f0,
                                      diffuseColor);
        }
        vec3 r = reflect(-v, n);
        vec3 prefiltered = prefilteredRadiance(r.z, roughness);
        vec3 ambient = (diffuseColor * ambientIrradiance(n.xzy) + prefiltered * environmentBRDF(f0, roughness, NoV)) *
                       (occlusion * uEnvParams.y);
        color = direct + ambient + emissive;
    }
    oCol = vec4(linearToSrgb(acesFitted(color)), base.a);
}
)";

const char *kDepthVertex = R"(#version 330 core
layout(location=0) in vec3 aPos;
layout(location=2) in vec2 aUv;
layout(location=3) in vec4 aCol;
uniform mat4 uViewProj;
out vec2 vUv; out float vAlpha;
void main() { vUv = aUv; vAlpha = aCol.a; gl_Position = uViewProj * vec4(aPos, 1.0); }
)";

// shadow.slang: alpha-tested materials cut holes into their shadows.
const char *kDepthFragment = R"(#version 330 core
in vec2 vUv; in float vAlpha;
uniform sampler2D uBaseTex;
uniform int uHasBase;
uniform float uBaseAlpha;
uniform float uCutoff;
void main()
{
    if (uCutoff > 0.0)
    {
        float alpha = vAlpha * uBaseAlpha;
        if (uHasBase != 0)
            alpha *= texture(uBaseTex, vUv).a;
        if (alpha < uCutoff)
            discard;
    }
}
)";

const char *kSkyVertex = R"(#version 330 core
out vec2 vNdc;
void main()
{
    vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2) * 2.0 - 1.0;
    vNdc = p;
    gl_Position = vec4(p, 1.0, 1.0);
}
)";

// sky.slang, with the engine's clear colour when no sky is active.
const char *kSkyFragment = R"(
in vec2 vNdc;
out vec4 oCol;
uniform mat4 uInvViewProj;
uniform vec3 uEye;
uniform int uSkyActive;
uniform vec3 uTop;
uniform vec3 uHorizon;
uniform vec3 uBottom;
uniform vec3 uSunColor;
uniform vec3 uSunDirection; // engine space
uniform vec4 uSkyParams;    // sun intensity, horizon exponent, cos(sun angle), cos(sun angle * 0.35)
void main()
{
    vec4 p = uInvViewProj * vec4(vNdc, 1.0, 1.0);
    vec3 d = normalize(p.xyz / p.w - uEye).xzy;
    vec3 color = vec3(0.07, 0.06, 0.10);
    if (uSkyActive != 0)
    {
        float blend = pow(clamp(abs(d.y), 0.0, 1.0), max(uSkyParams.y, 0.01));
        color = d.y >= 0.0 ? mix(uHorizon, uTop, blend) : mix(uHorizon, uBottom, blend);
        float sunDot = dot(d, normalize(-uSunDirection));
        color += uSunColor * (smoothstep(uSkyParams.z, uSkyParams.w, sunDot) * uSkyParams.x);
    }
    oCol = vec4(linearToSrgb(acesFitted(color)), 1.0);
}
)";

void copyMatrix(const QMatrix4x4 &m, float out[16])
{
    std::memcpy(out, m.constData(), sizeof(float) * 16);
}

QVector3D pickUp(const QVector3D &forward)
{
    return std::fabs(forward.z()) < 0.99f ? QVector3D(0, 0, 1) : QVector3D(0, 1, 0);
}

// render_common.cpp BuildCascade: bounding sphere of the camera frustum slice, snapped to texels.
QMatrix4x4 buildCascade(const QVector3D &eye, const QVector3D &forward, const QVector3D &right, const QVector3D &up,
                        float tanHalf, float aspect, float nearDepth, float farDepth, const QVector3D &lightDir,
                        int resolution, float &outTexelSize)
{
    const float metre = 1.f / ENGINE_MAP_UNIT_SCALE;
    QVector3D corners[8];
    int index = 0;
    for (float depth : {nearDepth, farDepth})
    {
        const QVector3D center = eye + forward * depth;
        const float h = depth * tanHalf;
        const float w = h * aspect;
        for (float sy : {-1.f, 1.f})
            for (float sx : {-1.f, 1.f})
                corners[index++] = center + right * (w * sx) + up * (h * sy);
    }
    QVector3D center;
    for (const QVector3D &corner : corners)
        center += corner;
    center /= 8.f;
    float radius = 0.f;
    for (const QVector3D &corner : corners)
        radius = std::max(radius, (corner - center).length());
    radius = std::ceil(radius / metre * 16.f) / 16.f * metre;

    const float casterExtend = 150.f * metre;
    const QVector3D shadowEye = center - lightDir * (radius + casterExtend);
    QMatrix4x4 view;
    view.lookAt(shadowEye, shadowEye + lightDir, pickUp(lightDir));
    QMatrix4x4 proj;
    proj.ortho(-radius, radius, -radius, radius, 0.f, radius * 2.f + casterExtend);
    QMatrix4x4 viewProj = proj * view;

    const float halfRes = float(resolution) * 0.5f;
    const QVector4D origin = viewProj * QVector4D(0.f, 0.f, 0.f, 1.f);
    const float x = origin.x() * halfRes;
    const float y = origin.y() * halfRes;
    viewProj(0, 3) += (std::round(x) - x) / halfRes;
    viewProj(1, 3) += (std::round(y) - y) / halfRes;

    outTexelSize = radius * 2.f / float(resolution);
    return viewProj;
}

GLuint createShadowArray(QOpenGLExtraFunctions *gl, int resolution, int layers)
{
    GLuint texture = 0;
    gl->glGenTextures(1, &texture);
    gl->glBindTexture(GL_TEXTURE_2D_ARRAY, texture);
    gl->glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_DEPTH_COMPONENT24, resolution, resolution, layers, 0, GL_DEPTH_COMPONENT,
                     GL_UNSIGNED_INT, nullptr);
    gl->glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    gl->glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    gl->glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl->glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    gl->glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
    gl->glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
    gl->glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
    return texture;
}
}

bool MaterialRenderer::initialize(QString *log)
{
    initializeOpenGLFunctions();
    const QByteArray meshFragment = QByteArray("#version 430 core\n") + kColourFunctions + kMeshFragment;
    const QByteArray skyFragment = QByteArray("#version 330 core\n") + kColourFunctions + kSkyFragment;
    bool ok = true;
    auto build = [&](QOpenGLShaderProgram &program, const char *vertex, const QByteArray &fragment, const char *name)
    {
        if (!program.addShaderFromSourceCode(QOpenGLShader::Vertex, vertex) ||
            !program.addShaderFromSourceCode(QOpenGLShader::Fragment, fragment) || !program.link())
        {
            ok = false;
            if (log)
                *log += QStringLiteral("%1 shader: %2\n").arg(QLatin1String(name), program.log());
        }
    };
    build(m_mesh, kMeshVertex, meshFragment, "Material");
    build(m_depth, kDepthVertex, kDepthFragment, "Shadow");
    build(m_sky, kSkyVertex, skyFragment, "Sky");
    m_vbo.create();
    m_shadowVao.create();
    if (!ok)
        return false;

    m_mesh.bind();
    const char *samplers[] = {"uBaseTex", "uNormalTex", "uMetallicRoughnessTex", "uOcclusionTex", "uEmissiveTex",
                              "uCascadeMap", "uLocalMap", "uEnvTable"};
    for (int i = 0; i < 8; ++i)
        m_mesh.setUniformValue(samplers[i], i);
    const GLuint blockIndex = glGetUniformBlockIndex(m_mesh.programId(), "SceneBlock");
    if (blockIndex != GL_INVALID_INDEX)
        glUniformBlockBinding(m_mesh.programId(), blockIndex, 0);
    m_mesh.release();
    m_depth.bind();
    m_depth.setUniformValue("uBaseTex", 0);
    m_depth.release();

    glGenBuffers(1, &m_sceneBlock);
    glBindBuffer(GL_UNIFORM_BUFFER, m_sceneBlock);
    glBufferData(GL_UNIFORM_BUFFER, sizeof(SceneBlock), nullptr, GL_DYNAMIC_DRAW);
    glBindBuffer(GL_UNIFORM_BUFFER, 0);
    glGenBuffers(1, &m_clusterLights);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_clusterLights);
    glBufferData(GL_SHADER_STORAGE_BUFFER, GLsizeiptr(sizeof(GpuLight)) * kMaxClusterLights, nullptr, GL_DYNAMIC_DRAW);
    glGenBuffers(1, &m_clusterGrid);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_clusterGrid);
    glBufferData(GL_SHADER_STORAGE_BUFFER, GLsizeiptr(sizeof(quint32)) * (kClusterCount + kMaxClusterIndices), nullptr,
                 GL_DYNAMIC_DRAW);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
    m_ready = true;
    return true;
}

void MaterialRenderer::release()
{
    releaseTextures();
    if (m_ready)
    {
        const GLuint textures[] = {m_cascadeMap, m_localMap, m_envTexture};
        glDeleteTextures(3, textures);
        glDeleteFramebuffers(1, &m_shadowFbo);
        const GLuint buffers[] = {m_sceneBlock, m_clusterLights, m_clusterGrid};
        glDeleteBuffers(3, buffers);
    }
    m_cascadeMap = m_localMap = m_envTexture = m_shadowFbo = m_sceneBlock = m_clusterLights = m_clusterGrid = 0;
    m_envUploaded = 0;
    m_vbo.destroy();
    m_shadowVao.destroy();
    m_mesh.removeAllShaders();
    m_depth.removeAllShaders();
    m_sky.removeAllShaders();
    m_ready = false;
}

void MaterialRenderer::releaseTextures()
{
    m_textures.clear();
}

void MaterialRenderer::setGeometry(const QVector<RenderVertex> &vertices, const QVector<Range> &ranges)
{
    m_ranges = ranges;
    m_vbo.bind();
    if (!vertices.isEmpty())
        m_vbo.allocate(vertices.constData(), int(vertices.size() * sizeof(RenderVertex)));
    m_vbo.release();
}

const MaterialInfo &MaterialRenderer::material(MaterialLibrary *materials, const QString &path) const
{
    static const MaterialInfo kDefault;
    const MaterialInfo *info = path.isEmpty() || !materials ? nullptr : materials->find(path);
    return info ? *info : kDefault;
}

QOpenGLTexture *MaterialRenderer::texture(const QString &material, Slot slot, const QImage &image)
{
    const QString key = material + QLatin1Char('#') + QString::number(int(slot));
    auto found = m_textures.constFind(key);
    if (found != m_textures.cend())
        return found->get();
    std::shared_ptr<QOpenGLTexture> created;
    if (!image.isNull())
    {
        created = std::make_shared<QOpenGLTexture>(image, QOpenGLTexture::GenerateMipMaps);
        created->setWrapMode(QOpenGLTexture::Repeat);
        created->setMaximumAnisotropy(8.f);
    }
    m_textures.insert(key, created);
    return created.get();
}

void MaterialRenderer::bindVertexAttributes(QOpenGLShaderProgram &program, bool full)
{
    m_vbo.bind();
    const int stride = sizeof(RenderVertex);
    program.enableAttributeArray(0);
    program.setAttributeBuffer(0, GL_FLOAT, 0, 3, stride);
    if (full)
    {
        program.enableAttributeArray(1);
        program.setAttributeBuffer(1, GL_FLOAT, 3 * sizeof(float), 3, stride);
    }
    program.enableAttributeArray(2);
    program.setAttributeBuffer(2, GL_FLOAT, 6 * sizeof(float), 2, stride);
    program.enableAttributeArray(3);
    program.setAttributeBuffer(3, GL_FLOAT, 8 * sizeof(float), 4, stride);
}

void MaterialRenderer::ensureShadowTargets()
{
    if (!m_cascadeMap)
        m_cascadeMap = createShadowArray(this, kCascadeResolution, kCascadeCount);
    if (!m_localMap)
        m_localMap = createShadowArray(this, kLocalResolution, kLocalSlices);
    if (!m_shadowFbo)
        glGenFramebuffers(1, &m_shadowFbo);
}

void MaterialRenderer::renderShadows(const QVector<ShadowView> &views, MaterialLibrary *materials)
{
    ensureShadowTargets();
    glBindFramebuffer(GL_FRAMEBUFFER, m_shadowFbo);
    const GLenum none = GL_NONE;
    glDrawBuffers(1, &none);
    glReadBuffer(GL_NONE);
    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glDepthFunc(GL_LESS);
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(1.5f, 2.f);

    QOpenGLVertexArrayObject::Binder vao(&m_shadowVao);
    m_depth.bind();
    bindVertexAttributes(m_depth, false);
    glActiveTexture(GL_TEXTURE0);
    for (const ShadowView &view : views)
    {
        const int resolution = view.cascade ? kCascadeResolution : kLocalResolution;
        glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, view.cascade ? m_cascadeMap : m_localMap, 0, view.slice);
        glViewport(0, 0, resolution, resolution);
        glClear(GL_DEPTH_BUFFER_BIT);
        m_depth.setUniformValue("uViewProj", view.viewProj);
        for (const Range &range : m_ranges)
        {
            const MaterialInfo &info = material(materials, range.material);
            if (!info.castShadows || info.transparent)
                continue;
            QOpenGLTexture *base = info.alphaCutoff > 0.f ? texture(range.material, BaseSlot, info.texture) : nullptr;
            m_depth.setUniformValue("uCutoff", info.alphaCutoff);
            m_depth.setUniformValue("uBaseAlpha", info.baseColor[3]);
            m_depth.setUniformValue("uHasBase", base ? 1 : 0);
            if (base)
                base->bind(0);
            glDrawArrays(GL_TRIANGLES, range.first, range.count);
        }
    }
    m_depth.disableAttributeArray(2);
    m_depth.disableAttributeArray(3);
    m_depth.release();
    m_vbo.release();
    glDisable(GL_POLYGON_OFFSET_FILL);
}

void MaterialRenderer::uploadEnvironment()
{
    if (m_envTexture && m_envUploaded == m_environment.key)
        return;
    if (!m_envTexture)
        glGenTextures(1, &m_envTexture);
    glBindTexture(GL_TEXTURE_2D, m_envTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, LitEnvironment::kTableSize, LitEnvironment::kMipCount, 0, GL_RGBA, GL_FLOAT,
                 m_environment.table.constData());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
    m_envUploaded = m_environment.key;
}

void MaterialRenderer::prepareLit(const MapDocument &doc, const Camera &cam, MaterialLibrary *materials, GLuint targetFbo,
                                  QSize targetSize)
{
    if (!m_ready)
        return;
    const LitScene scene = gatherLitScene(doc);
    m_skySettings = scene.sky;
    updateLitEnvironment(m_skySettings, m_environment);
    uploadEnvironment();

    const float metre = 1.f / ENGINE_MAP_UNIT_SCALE;
    const QVector3D eye = cam.pos;
    const QVector3D forward = cam.forward().normalized();
    const QVector3D right = cam.right().normalized();
    const QVector3D up = QVector3D::crossProduct(right, forward).normalized();

    SceneBlock block = {};
    block.cameraPosition[0] = eye.x();
    block.cameraPosition[1] = eye.y();
    block.cameraPosition[2] = eye.z();
    block.cameraForward[0] = forward.x();
    block.cameraForward[1] = forward.y();
    block.cameraForward[2] = forward.z();
    std::memcpy(block.ambientSH, m_environment.sh, sizeof(block.ambientSH));
    block.environmentParams[0] = float(LitEnvironment::kMipCount);
    block.environmentParams[1] = 1.f;

    const float aspect = float(std::max(1, cam.size.width())) / float(std::max(1, cam.size.height()));
    const float vfov = std::min(2.f * std::atan(std::tan(cam.fovH * kPi / 360.f) / aspect), 150.f * kPi / 180.f);
    const float tanHalf = std::tan(vfov * 0.5f);

    // Cluster grid in view space (right, up, forward); the shader's clusterHeader does the same math.
    const float clusterNear = kCameraNearMetres * metre;
    const float clusterFar = kClusterFarMetres * metre;
    const float sliceScale = float(kClusterZ) / std::log(clusterFar / clusterNear);
    const float yScale = 1.f / tanHalf;
    const float xScale = yScale / aspect;
    struct ClusterRange
    {
        int x0, x1, y0, y1, z0, z1;
    };
    // False when the sphere can't touch the view.
    auto clusterRange = [&](const QVector3D &center, float radius, ClusterRange &out)
    {
        const QVector3D d = center - eye;
        const float vx = QVector3D::dotProduct(d, right);
        const float vy = QVector3D::dotProduct(d, up);
        const float vz = QVector3D::dotProduct(d, forward);
        if (vz + radius < clusterNear)
            return false;
        const float zMin = std::max(vz - radius, clusterNear);
        const float zMax = vz + radius;
        float nx0 = 1e30f, nx1 = -1e30f, ny0 = 1e30f, ny1 = -1e30f;
        for (float z : {zMin, zMax})
            for (float sign : {-1.f, 1.f})
            {
                const float x = (vx + sign * radius) * xScale / z;
                const float y = (vy + sign * radius) * yScale / z;
                nx0 = std::min(nx0, x);
                nx1 = std::max(nx1, x);
                ny0 = std::min(ny0, y);
                ny1 = std::max(ny1, y);
            }
        if (nx1 < -1.f || nx0 > 1.f || ny1 < -1.f || ny0 > 1.f)
            return false;
        auto tile = [](float ndc, int count)
        { return std::clamp(int(std::floor((ndc * 0.5f + 0.5f) * float(count))), 0, count - 1); };
        auto slice = [&](float z)
        { return std::clamp(int(std::floor(std::log(std::max(z, clusterNear) / clusterNear) * sliceScale)), 0, kClusterZ - 1); };
        out = {tile(nx0, kClusterX), tile(nx1, kClusterX), tile(ny0, kClusterY), tile(ny1, kClusterY), slice(zMin), slice(zMax)};
        return true;
    };

    // Directional lights into the block in map order, visible local lights into the clusters
    // nearest first (BuildFrameSetup). Spots are bounded by their cone + cap sphere when smaller.
    struct Candidate
    {
        const LitLight *light = nullptr;
        float score = 0.f;
        ClusterRange range = {};
    };
    QVector<Candidate> candidates;
    candidates.reserve(scene.lights.size());
    for (const LitLight &light : scene.lights)
    {
        if (light.type == LitLight::Directional)
        {
            candidates.push_back({&light, -1e30f, {}});
            continue;
        }
        QVector3D center = light.position;
        float radius = std::max(light.range, 0.01f * metre);
        if (light.type == LitLight::Spot)
        {
            const float cosHalf = std::cos(std::clamp(light.spotAngle, 1.f, 179.f) * 0.5f * kPi / 180.f);
            const float coneRadius = radius / (2.f * cosHalf);
            if (coneRadius < radius)
            {
                center = light.position + light.direction.normalized() * coneRadius;
                radius = coneRadius;
            }
        }
        Candidate candidate{&light, (center - eye).length() - radius, {}};
        if (clusterRange(center, radius, candidate.range))
            candidates.push_back(candidate);
    }
    std::stable_sort(candidates.begin(), candidates.end(), [](const Candidate &a, const Candidate &b) { return a.score < b.score; });

    std::vector<GpuLight> clusterLights;
    std::vector<ClusterRange> ranges;
    clusterLights.reserve(std::min<size_t>(size_t(candidates.size()), kMaxClusterLights));
    ranges.reserve(clusterLights.capacity());
    QVector<ShadowView> views;
    bool sunAssigned = false;
    int nextLocalSlice = 0;
    int lightCount = 0;
    for (const Candidate &candidate : candidates)
    {
        const LitLight *light = candidate.light;
        GpuLight *slot = nullptr;
        if (light->type == LitLight::Directional)
        {
            if (lightCount >= kMaxDirectionalLights)
                continue;
            slot = &block.lights[lightCount++];
        }
        else
        {
            if (clusterLights.size() >= size_t(kMaxClusterLights))
                break;
            clusterLights.push_back({});
            ranges.push_back(candidate.range);
            slot = &clusterLights.back();
        }
        GpuLight &gpu = *slot;
        const float halfAngle = std::clamp(light->spotAngle, 1.f, 179.f) * 0.5f * kPi / 180.f;
        gpu.position[0] = light->position.x();
        gpu.position[1] = light->position.y();
        gpu.position[2] = light->position.z();
        gpu.position[3] = light->range;
        gpu.color[0] = light->radiance.x();
        gpu.color[1] = light->radiance.y();
        gpu.color[2] = light->radiance.z();
        gpu.direction[0] = light->direction.x();
        gpu.direction[1] = light->direction.y();
        gpu.direction[2] = light->direction.z();
        gpu.direction[3] = std::cos(halfAngle);
        gpu.params[0] = float(light->type);
        gpu.params[1] = std::cos(halfAngle * 0.75f);
        gpu.params[2] = -1.f;
        if (!light->castShadows)
            continue;

        if (light->type == LitLight::Directional)
        {
            if (sunAssigned)
                continue;
            sunAssigned = true;
            gpu.params[2] = 1.f;
            const float nearPlane = kCameraNearMetres * metre;
            const float farPlane = kShadowDistanceMetres * metre;
            float splits[kCascadeCount + 1] = {nearPlane};
            for (int c = 1; c <= kCascadeCount; ++c)
            {
                const float t = float(c) / float(kCascadeCount);
                const float logSplit = nearPlane * std::pow(farPlane / nearPlane, t);
                const float linearSplit = nearPlane + (farPlane - nearPlane) * t;
                splits[c] = 0.8f * logSplit + 0.2f * linearSplit;
            }
            for (int c = 0; c < kCascadeCount; ++c)
            {
                float texel = 0.f;
                ShadowView view;
                view.viewProj = buildCascade(eye, forward, right, up, tanHalf, aspect, splits[c], splits[c + 1],
                                             light->direction.normalized(), kCascadeResolution, texel);
                view.cascade = true;
                view.slice = c;
                views.push_back(view);
                copyMatrix(view.viewProj, block.cascadeViewProj[c]);
                block.cascadeSplits[c] = splits[c + 1];
                block.cascadeTexelSize[c] = texel;
            }
            block.shadowParams[0] = float(kCascadeCount);
            continue;
        }

        // A spot uses one slice, a point light six (+X -X +Y -Y +Z -Z, editor space).
        const int needed = light->type == LitLight::Point ? 6 : 1;
        if (nextLocalSlice + needed > kLocalSlices)
            continue;
        gpu.params[2] = float(nextLocalSlice);
        const float range = std::max(light->range, 0.1f * metre);
        const float zn = std::max(0.05f * metre, range * 0.002f);
        if (light->type == LitLight::Spot)
        {
            const QVector3D dir = light->direction.normalized();
            const float fov = std::min(std::clamp(light->spotAngle, 1.f, 179.f) * kPi / 180.f + 0.1f, 3.f);
            QMatrix4x4 view, proj;
            view.lookAt(light->position, light->position + dir, pickUp(dir));
            proj.perspective(fov * 180.f / kPi, 1.f, zn, range);
            ShadowView shadow;
            shadow.viewProj = proj * view;
            shadow.slice = nextLocalSlice;
            views.push_back(shadow);
            copyMatrix(shadow.viewProj, block.localViewProj[nextLocalSlice]);
        }
        else
        {
            static const QVector3D kDirs[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
            QMatrix4x4 proj;
            proj.perspective(92.f, 1.f, zn, range);
            for (int face = 0; face < 6; ++face)
            {
                QMatrix4x4 view;
                view.lookAt(light->position, light->position + kDirs[face], pickUp(kDirs[face]));
                ShadowView shadow;
                shadow.viewProj = proj * view;
                shadow.slice = nextLocalSlice + face;
                views.push_back(shadow);
                copyMatrix(shadow.viewProj, block.localViewProj[nextLocalSlice + face]);
            }
        }
        nextLocalSlice += needed;
    }
    block.lightCounts[0] = float(lightCount);

    // Bin the cluster lights: count, reserve each cluster's index range, fill.
    std::vector<quint32> grid(size_t(kClusterCount), 0u);
    if (!clusterLights.empty())
    {
        std::vector<quint32> counts(size_t(kClusterCount), 0u);
        for (const ClusterRange &r : ranges)
            for (int z = r.z0; z <= r.z1; ++z)
                for (int y = r.y0; y <= r.y1; ++y)
                    for (int x = r.x0; x <= r.x1; ++x)
                    {
                        quint32 &count = counts[size_t((z * kClusterY + y) * kClusterX + x)];
                        count = std::min(count + 1u, kMaxLightsPerCluster);
                    }
        quint32 offset = 0;
        for (int c = 0; c < kClusterCount; ++c)
        {
            quint32 count = std::min(counts[size_t(c)], quint32(kMaxClusterIndices) - offset);
            grid[size_t(c)] = offset << 8;
            counts[size_t(c)] = count;
            offset += count;
        }
        grid.resize(size_t(kClusterCount) + offset, 0u);
        for (quint32 li = 0; li < quint32(ranges.size()); ++li)
        {
            const ClusterRange &r = ranges[li];
            for (int z = r.z0; z <= r.z1; ++z)
                for (int y = r.y0; y <= r.y1; ++y)
                    for (int x = r.x0; x <= r.x1; ++x)
                    {
                        const size_t c = size_t((z * kClusterY + y) * kClusterX + x);
                        const quint32 filled = grid[c] & 0xFFu;
                        if (filled >= counts[c])
                            continue;
                        grid[size_t(kClusterCount) + (grid[c] >> 8) + filled] = li;
                        grid[c] += 1u;
                    }
        }
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_clusterLights);
        glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, GLsizeiptr(clusterLights.size() * sizeof(GpuLight)), clusterLights.data());
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_clusterGrid);
        glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, GLsizeiptr(grid.size() * sizeof(quint32)), grid.data());
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
    }
    const QVector3D clusterSpaces[2] = {right, up};
    for (int axis = 0; axis < 2; ++axis)
    {
        float *out = axis == 0 ? block.clusterRight : block.clusterUp;
        out[0] = clusterSpaces[axis].x();
        out[1] = clusterSpaces[axis].y();
        out[2] = clusterSpaces[axis].z();
        out[3] = axis == 0 ? xScale : yScale;
    }
    block.clusterParams[0] = float(kClusterX);
    block.clusterParams[1] = float(kClusterY);
    block.clusterParams[2] = float(kClusterZ);
    block.clusterParams[3] = float(clusterLights.size());
    block.clusterDepth[0] = clusterNear;
    block.clusterDepth[1] = sliceScale;
    block.shadowParams[1] = 1.f / float(kCascadeResolution);
    block.shadowParams[2] = 1.f / float(kLocalResolution);
    block.shadowParams[3] = views.isEmpty() ? 0.f : 1.f;

    glBindBuffer(GL_UNIFORM_BUFFER, m_sceneBlock);
    glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(SceneBlock), &block);
    glBindBuffer(GL_UNIFORM_BUFFER, 0);

    if (!views.isEmpty() && !m_ranges.isEmpty())
        renderShadows(views, materials);
    glBindFramebuffer(GL_FRAMEBUFFER, targetFbo);
    glViewport(0, 0, targetSize.width(), targetSize.height());
}

void MaterialRenderer::drawSky(const Camera &cam)
{
    if (!m_ready)
        return;
    const LitSky &sky = m_skySettings;
    const float sunAngle = sky.sunSize * kPi / 180.f;
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    m_sky.bind();
    m_sky.setUniformValue("uInvViewProj", cam.invViewProj);
    m_sky.setUniformValue("uEye", cam.pos);
    m_sky.setUniformValue("uSkyActive", sky.active ? 1 : 0);
    m_sky.setUniformValue("uTop", sky.top);
    m_sky.setUniformValue("uHorizon", sky.horizon);
    m_sky.setUniformValue("uBottom", sky.bottom);
    m_sky.setUniformValue("uSunColor", sky.sunColor);
    m_sky.setUniformValue("uSunDirection", sky.sunDirection);
    m_sky.setUniformValue("uSkyParams", QVector4D(sky.sunIntensity, sky.horizonExponent, std::cos(sunAngle), std::cos(sunAngle * 0.35f)));
    glDrawArrays(GL_TRIANGLES, 0, 3);
    m_sky.release();
    glDepthMask(GL_TRUE);
}

void MaterialRenderer::draw(ShadingMode mode, const Camera &cam, MaterialLibrary *materials)
{
    if (!m_ready || m_ranges.isEmpty())
        return;
    const bool lit = mode == ShadingMode::Lit;
    m_mesh.bind();
    m_mesh.setUniformValue("uViewProj", cam.viewProj);
    m_mesh.setUniformValue("uMode", int(mode));
    bindVertexAttributes(m_mesh, true);
    glBindBufferBase(GL_UNIFORM_BUFFER, 0, m_sceneBlock);
    if (lit)
    {
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, m_clusterLights);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, m_clusterGrid);
        glActiveTexture(GL_TEXTURE5);
        glBindTexture(GL_TEXTURE_2D_ARRAY, m_cascadeMap);
        glActiveTexture(GL_TEXTURE6);
        glBindTexture(GL_TEXTURE_2D_ARRAY, m_localMap);
        glActiveTexture(GL_TEXTURE7);
        glBindTexture(GL_TEXTURE_2D, m_envTexture);
    }

    const QOpenGLTexture::Filter minFilter = mode == ShadingMode::Shaded ? QOpenGLTexture::NearestMipMapNearest : QOpenGLTexture::LinearMipMapLinear;
    const QOpenGLTexture::Filter magFilter = mode == ShadingMode::Shaded ? QOpenGLTexture::Nearest : QOpenGLTexture::Linear;
    // Opaque first; transparent after without depth writes, as the engine's transparent pass.
    for (int pass = 0; pass < 2; ++pass)
    {
        glDepthMask(pass == 0 ? GL_TRUE : GL_FALSE);
        for (const Range &range : m_ranges)
        {
            const MaterialInfo &info = material(materials, range.material);
            if (info.transparent != (pass == 1))
                continue;
            const QImage *images[SlotCount] = {&info.texture, &info.normalTexture, &info.metallicRoughnessTexture,
                                               &info.occlusionTexture, &info.emissiveTexture};
            int flags = (info.lit ? FlagLit : 0) | (info.doubleSided ? FlagDoubleSided : 0);
            const int hasFlags[SlotCount] = {FlagHasBase, FlagHasNormal, FlagHasMetallicRoughness, FlagHasOcclusion, FlagHasEmissive};
            for (int slot = 0; slot < SlotCount; ++slot)
            {
                // Shaded only shows the base texture
                if (slot != BaseSlot && !lit && slot != EmissiveSlot)
                    continue;
                QOpenGLTexture *tex = texture(range.material, Slot(slot), *images[slot]);
                if (!tex)
                    continue;
                tex->setMinMagFilters(minFilter, magFilter);
                tex->bind(GLuint(slot));
                flags |= hasFlags[slot];
            }
            m_mesh.setUniformValue("uFlags", flags);
            m_mesh.setUniformValue("uBaseColor", QVector4D(info.baseColor[0], info.baseColor[1], info.baseColor[2], info.baseColor[3]));
            const float e = info.emissiveIntensity;
            m_mesh.setUniformValue("uEmissive", QVector4D(info.emissive[0] * e, info.emissive[1] * e, info.emissive[2] * e, info.alphaCutoff));
            m_mesh.setUniformValue("uPbr", QVector4D(info.metallic, info.roughness, info.normalScale, std::clamp(info.occlusionStrength, 0.f, 1.f)));
            glDrawArrays(GL_TRIANGLES, range.first, range.count);
        }
    }
    glDepthMask(GL_TRUE);
    glActiveTexture(GL_TEXTURE0);
    m_mesh.disableAttributeArray(2);
    m_mesh.disableAttributeArray(3);
    m_mesh.release();
    m_vbo.release();
}
