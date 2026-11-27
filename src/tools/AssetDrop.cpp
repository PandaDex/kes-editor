#include "tools/AssetDrop.h"
#include "core/EditorContext.h"
#include "core/MaterialLibrary.h"
#include "core/ModelCache.h"
#include "entities/EntityDefs.h"
#include "render/Viewport.h"
#include "ui/AssetDrag.h"
#include <QFileInfo>
#include <QMimeData>

namespace
{
    struct EntityAsset
    {
        const char *suffix;
        const char *className;
        const char *key;
    };
    // kmodel picks StaticMeshEntity or SkinnedModelEntity from the model itself
    const EntityAsset kEntityAssets[] = {{"kmodel", "StaticMeshEntity", "Model"},
                                         {"kfx", "ParticleSystemEntity", "System"},
                                         {"ksound", "AudioSourceEntity", "Sound"}};

    void setKey(MapObject &o, const QString &key, const QString &value)
    {
        for (auto it = o.kv.begin(); it != o.kv.end(); ++it)
        {
            if (it.key().compare(key, Qt::CaseInsensitive) == 0)
            {
                it.value() = value;
                return;
            }
        }
        o.kv[key] = value;
    }
}

bool AssetDrop::MaterialTarget::operator==(const MaterialTarget &o) const
{
    return type == o.type && object == o.object && face == o.face && slot == o.slot;
}

AssetDrop *AssetDrop::create(EditorContext *ctx, Viewport *vp, const QMimeData *mime)
{
    const QString material = draggedAssetPath(mime, ctx->materials()->root(), {"mat"});
    if (!material.isEmpty())
        return new AssetDrop(ctx, vp, Kind::Material, material);

    for (const EntityAsset &asset : kEntityAssets)
    {
        const QString path = draggedAssetPath(mime, ctx->projectDir(), {QString::fromLatin1(asset.suffix)});
        if (path.isEmpty())
            continue;
        QString className = QString::fromLatin1(asset.className);
        if (className == QLatin1String("StaticMeshEntity"))
            if (const ModelAsset *model = ctx->models()->find(path); model && model->skinned)
                className = QStringLiteral("SkinnedModelEntity");
        const ClassDef *def = ctx->registry()->resolved(className);
        if (!def)
            return nullptr;
        auto *drop = new AssetDrop(ctx, vp, Kind::Entity, path);
        applyClassDefaults(drop->m_entity, *def);
        setKey(drop->m_entity, QString::fromLatin1(asset.key), path);
        drop->m_entity.name = ctx->doc()->uniqueName(QFileInfo(path).completeBaseName());
        return drop;
    }
    return nullptr;
}

AssetDrop::AssetDrop(EditorContext *ctx, Viewport *vp, Kind kind, const QString &path)
    : m_ctx(ctx), m_vp(vp), m_kind(kind), m_path(path)
{
    if (kind == Kind::Material && !ctx->materials()->find(path))
        ctx->materials()->reload(); // a new .mat, or one outside the scanned folder
}

AssetDrop::~AssetDrop()
{
    if (!m_done)
        cancel();
}

const ModelAsset *AssetDrop::modelOf(const MapObject &o) const
{
    if (o.solid)
        return nullptr;
    const ClassDef *def = o.classname.isEmpty() ? nullptr : m_ctx->registry()->resolved(o.classname);
    return m_ctx->models()->find(objectValue(o, "Model", def ? def->model : QString()));
}

AssetDrop::MaterialTarget AssetDrop::materialTarget(const QPointF &pos, bool wholeObject) const
{
    MaterialTarget target;
    if (m_vp->isOrtho())
    {
        const ObjectId id = m_vp->pickObject(pos);
        const MapObject *o = id ? m_ctx->doc()->find(id) : nullptr;
        if (o && o->solid)
            target = {MaterialTarget::Solid, id, {}, -1};
        else if (o && modelOf(*o))
            target = {MaterialTarget::Model, id, {}, -1};
        return target;
    }

    float best = 1e30f;
    MaterialFaceRef face;
    float faceT = 0.f;
    if (MaterialOps::pickFace(m_ctx, m_vp, pos, face, &faceT))
    {
        best = faceT;
        target = {wholeObject ? MaterialTarget::Solid : MaterialTarget::Face, face.object, wholeObject ? MaterialFaceRef{} : face, -1};
    }
    const Ray ray = m_vp->camera().ray(pos);
    for (const auto &entry : m_ctx->doc()->objects())
    {
        const MapObject &o = entry.second;
        if (o.solid || !m_ctx->doc()->isVisible(o))
            continue;
        const ModelAsset *model = modelOf(o);
        if (!model)
            continue;
        float t = 0.f;
        const int section = ModelCache::pickSection(*model, ModelCache::placement(o), ray, &t);
        if (section < 0 || t >= best)
            continue;
        best = t;
        target = wholeObject ? MaterialTarget{MaterialTarget::Model, o.id, {}, -1}
                             : MaterialTarget{MaterialTarget::ModelPart, o.id, {}, model->sections[section].slot};
    }
    return target;
}

// Same precedence as the engine: Material.<slot> beats the slot's own material, which beats Material.
MapObject AssetDrop::withMaterial(const MapObject &original, const MaterialTarget &target) const
{
    MapObject o = original;
    switch (target.type)
    {
    case MaterialTarget::Face:
        o.ensureBrushes();
        if (target.face.brush >= 0 && target.face.brush < o.brushes.size() && target.face.face >= 0 &&
            target.face.face < o.brushes[target.face.brush].faces.size())
            o.brushes[target.face.brush].faces[target.face.face].material = m_path;
        break;
    case MaterialTarget::Solid:
        o.ensureBrushes();
        for (Brush &b : o.brushes)
            b.setMaterial(m_path);
        break;
    case MaterialTarget::ModelPart:
    case MaterialTarget::Model:
    {
        const ModelAsset *model = modelOf(o);
        const bool whole = target.type == MaterialTarget::Model;
        if (whole || target.slot < 0)
            setKey(o, "Material", m_path);
        if (model)
            for (int i = 0; i < model->materialSlots.size(); ++i)
                if (whole || i == target.slot)
                    setKey(o, "Material." + model->materialSlots[i].name, m_path);
        break;
    }
    case MaterialTarget::None:
        break;
    }
    return o;
}

void AssetDrop::previewMaterial(const MaterialTarget &target)
{
    if (target == m_target)
        return;
    QVector<MapObject> changed;
    for (auto it = m_originals.cbegin(); it != m_originals.cend(); ++it)
        if (it.key() != target.object)
            changed.push_back(it.value());
    if (target.type != MaterialTarget::None)
    {
        if (!m_originals.contains(target.object))
            if (const MapObject *o = m_ctx->doc()->find(target.object))
                m_originals.insert(target.object, *o);
        if (m_originals.contains(target.object))
            changed.push_back(withMaterial(m_originals[target.object], target));
    }
    m_target = target;
    if (!changed.isEmpty())
        m_ctx->doc()->previewReplace(changed);
}

bool AssetDrop::placementPoint(const QPointF &pos, QVector3D &out) const
{
    if (m_vp->isOrtho())
    {
        const Camera &cam = m_vp->camera();
        if (!m_vp->planePoint(pos, cam.axisD(), 0.f, out))
            return false;
        const QVector3D snapped = m_ctx->snap(out);
        out[cam.axisH()] = snapped[cam.axisH()];
        out[cam.axisV()] = snapped[cam.axisV()];
        return true;
    }
    // Surfaces of brushes and models, not other point entities or the preview itself.
    const Ray ray = m_vp->camera().ray(pos);
    float best = 1e30f;
    for (const auto &entry : m_ctx->doc()->objects())
    {
        const MapObject &o = entry.second;
        if (o.id == m_preview || !m_ctx->doc()->isVisible(o))
            continue;
        float t = 0.f;
        bool hit = false;
        if (o.solid)
            hit = o.intersectRay(ray, &t);
        else if (const ModelAsset *model = modelOf(o))
            hit = ModelCache::pickSection(*model, ModelCache::placement(o), ray, &t) >= 0;
        if (hit && t > 0.f && t < best)
            best = t;
    }
    if (best < 1e29f)
        out = ray.origin + ray.dir * best;
    else if (!m_vp->planePoint(pos, 2, 0.f, out))
        out = ray.origin + ray.dir * 256.f;
    return true;
}

void AssetDrop::placeEntity(const QPointF &pos)
{
    QVector3D point;
    if (!placementPoint(pos, point))
        return;
    MapObject o = m_entity;
    o.origin = point;
    // 3D: stand the model on the surface
    if (!m_vp->isOrtho())
    {
        if (const ModelAsset *model = modelOf(o))
        {
            MapObject atZero = o;
            atZero.origin = {};
            const AABB local = ModelCache::worldBounds(*model, ModelCache::placement(atZero));
            if (local.valid())
                o.origin.setZ(point.z() - local.mn.z());
        }
    }
    if (!m_preview)
    {
        m_preview = m_ctx->doc()->previewInsert(o);
        return;
    }
    o.id = m_preview;
    if (const MapObject *current = m_ctx->doc()->find(m_preview))
        o.guid = current->guid;
    m_ctx->doc()->previewReplace({o});
}

void AssetDrop::move(const QPointF &pos, bool wholeObject)
{
    if (m_kind == Kind::Material)
        previewMaterial(materialTarget(pos, wholeObject));
    else
        placeEntity(pos);
}

void AssetDrop::cancel()
{
    m_done = true;
    if (m_kind == Kind::Material)
    {
        if (!m_originals.isEmpty())
            m_ctx->doc()->previewReplace(m_originals.values().toVector());
        m_originals.clear();
        m_target = {};
    }
    else if (m_preview)
    {
        m_ctx->doc()->previewRemove(m_preview);
        m_preview = 0;
    }
}

void AssetDrop::commit(const QPointF &pos, bool wholeObject)
{
    move(pos, wholeObject);
    m_done = true;
    MapDocument *doc = m_ctx->doc();
    if (m_kind == Kind::Material)
    {
        m_ctx->setCurrentMaterial(m_path);
        if (!m_originals.isEmpty())
            doc->commitFromPreview(m_originals.values().toVector(), QObject::tr("Apply Material"));
        return;
    }
    const MapObject *placed = m_preview ? doc->find(m_preview) : nullptr;
    if (!placed)
        return;
    MapObject o = *placed;
    doc->previewRemove(m_preview);
    m_preview = 0;
    o.id = 0;
    o.guid = {};
    const ObjectId id = doc->addObject(o, QObject::tr("Place %1").arg(QFileInfo(m_path).fileName()));
    if (id)
        doc->setSelection({id});
}
