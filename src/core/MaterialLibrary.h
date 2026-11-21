#pragma once
#include <QColor>
#include <QHash>
#include <QImage>
#include <QObject>
#include <QSize>
#include <QString>
#include <QStringList>

// Paths are content-relative with forward slashes ("materials/brick.mat"), exactly what
// AssetSystem::LoadMaterial expects, so they go into compiled maps unchanged.
struct MaterialInfo
{
    QString path;
    QString name;
    QColor color{255, 255, 255};
    QString texturePath;
    QImage texture;
    bool transparent = false;

    // Engine material values (material_file.cpp), used by the Unlit and Lit views.
    float baseColor[4] = {1.f, 1.f, 1.f, 1.f};
    float emissive[3] = {0.f, 0.f, 0.f};
    float emissiveIntensity = 1.f;
    float metallic = 0.f;
    float roughness = 0.8f;
    float normalScale = 1.f;
    float occlusionStrength = 1.f;
    float alphaCutoff = 0.f;
    bool lit = true;
    bool doubleSided = false;
    bool castShadows = true;
    QImage normalTexture;
    QImage metallicRoughnessTexture;
    QImage occlusionTexture;
    QImage emissiveTexture;

    // Pixel size used for texture coordinates. Untextured materials use a nominal
    // 64x64 so UVs stay meaningful if a texture is added later.
    QSize textureSize() const { return texture.isNull() ? QSize(64, 64) : texture.size(); }
};

class MaterialLibrary : public QObject
{
    Q_OBJECT
public:
    explicit MaterialLibrary(QObject *parent = nullptr);

    // contentDir: root that material/texture paths are relative to.
    // scanDir: where to look for .mat files ("" = whole content dir).
    // texturesDir: fallback folder for texture paths that don't resolve from contentDir.
    void setRoot(const QString &contentDir, const QString &scanDir = QString(), const QString &texturesDir = QString());
    const QString &root() const { return m_root; }
    const QString &scanDir() const { return m_scanDir; }
    void reload();

    QStringList paths() const { return m_order; }
    const MaterialInfo *find(const QString &path) const;
    QSize textureSize(const QString &path) const;

    static QImage loadTexture(const QString &absolutePath, QString *error = nullptr);

signals:
    void changed();

private:
    MaterialInfo parse(const QString &relativePath) const;
    QImage loadMaterialTexture(const QString &path) const;

    QString m_root;
    QString m_scanDir;
    QString m_texturesDir;
    QHash<QString, MaterialInfo> m_materials;
    QStringList m_order;
};
