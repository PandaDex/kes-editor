#pragma once
#include "core/MapObject.h"
#include "tools/MaterialTool.h"
#include <QMap>
#include <QPointF>
#include <QString>

class EditorContext;
class QMimeData;
class Viewport;
struct ModelAsset;

// Assets dragged from the Asset Browser into a viewport, previewed live while the drag moves:
// a material goes on the face, brush or model part under the cursor (Shift: the whole object),
// a model / particle system / sound becomes an entity placed under the cursor.
class AssetDrop
{
public:
    // nullptr when the drag carries nothing a viewport takes.
    static AssetDrop *create(EditorContext *ctx, Viewport *vp, const QMimeData *mime);
    ~AssetDrop();

    void move(const QPointF &pos, bool wholeObject);
    void cancel();
    void commit(const QPointF &pos, bool wholeObject);

private:
    enum class Kind
    {
        Material,
        Entity
    };
    struct MaterialTarget
    {
        enum Type
        {
            None,
            Face,
            Solid,
            ModelPart,
            Model
        } type = None;
        ObjectId object = 0;
        MaterialFaceRef face;
        int slot = -1; // ModelPart: material slot, -1 = the entity Material
        bool operator==(const MaterialTarget &o) const;
    };

    AssetDrop(EditorContext *ctx, Viewport *vp, Kind kind, const QString &path);
    MaterialTarget materialTarget(const QPointF &pos, bool wholeObject) const;
    MapObject withMaterial(const MapObject &o, const MaterialTarget &target) const;
    void previewMaterial(const MaterialTarget &target);
    bool placementPoint(const QPointF &pos, QVector3D &out) const;
    void placeEntity(const QPointF &pos);
    const ModelAsset *modelOf(const MapObject &o) const;

    EditorContext *m_ctx;
    Viewport *m_vp;
    Kind m_kind;
    QString m_path;
    bool m_done = false;

    // material: originals of every object the preview touched
    QMap<ObjectId, MapObject> m_originals;
    MaterialTarget m_target;

    // entity: the preview object
    MapObject m_entity;
    ObjectId m_preview = 0;
};
