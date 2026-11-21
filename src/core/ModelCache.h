#pragma once
#include "core/Math.h"
#include <QDateTime>
#include <QHash>
#include <QMatrix4x4>
#include <QObject>
#include <QSet>
#include <QVector>
#include <memory>

struct MapObject;

struct ModelVertex
{
    QVector3D position; // engine model space (metres, Y up)
    QVector3D normal;
    float u = 0.f, v = 0.f;
    float r = 1.f, g = 1.f, b = 1.f, a = 1.f;
};

// A .kmodel flattened the same way the engine draws it: every mesh, node transforms baked, bind pose.
struct ModelAsset
{
    QVector<ModelVertex> vertices;
    QVector<quint32> indices;
    AABB bounds; // engine model space
};

// .kmodel assets shared by every viewport. Paths are content-relative like the map's "Model" values.
class ModelCache : public QObject
{
    Q_OBJECT
public:
    explicit ModelCache(QObject *parent = nullptr) : QObject(parent) {}

    void setRoot(const QString &contentDir);
    // nullptr when the path is empty, missing or not a loadable .kmodel (logged once per change).
    const ModelAsset *find(const QString &path);
    void clear();
    // Asks views to redraw; files changed on disk since their last load are re-read then.
    void refresh() { emit changed(); }

    // Engine model space -> editor world space for a point entity, matching how the engine
    // places it (Angles, Scale, unit and axis conversion).
    static QMatrix4x4 placement(const MapObject &o);
    static AABB worldBounds(const ModelAsset &model, const QMatrix4x4 &placement);

signals:
    void changed();
    void message(const QString &text, int level);

private:
    struct Entry
    {
        std::shared_ptr<ModelAsset> asset; // null = failed
        QDateTime modified;
    };
    QString m_root;
    QHash<QString, Entry> m_entries;
};

// Case-insensitive key lookup: .dec property names ("Model") and older lowercase keys both occur.
QString objectValue(const MapObject &o, const QString &key, const QString &fallback = QString());
