#include "core/ModelCache.h"
#include "core/MapObject.h"

#include "engine_configuration.h"
#include "kmodel.h"

#include <QDir>
#include <QFileInfo>

namespace
{
constexpr int kWarning = 1;

QVector3D parseScale(const QString &text)
{
    const QStringList parts = text.simplified().split(' ', Qt::SkipEmptyParts);
    QVector3D s(1.f, 1.f, 1.f);
    bool ok = false;
    if (parts.size() == 1)
    {
        const float v = parts[0].toFloat(&ok);
        return ok ? QVector3D(v, v, v) : s;
    }
    for (int i = 0; i < 3 && i < parts.size(); ++i)
    {
        const float v = parts[i].toFloat(&ok);
        if (ok)
            s[i] = v;
    }
    return s;
}
}

QString objectValue(const MapObject &o, const QString &key, const QString &fallback)
{
    for (auto it = o.kv.cbegin(); it != o.kv.cend(); ++it)
    {
        if (it.key().compare(key, Qt::CaseInsensitive) == 0)
            return it.value();
    }
    return fallback;
}

void ModelCache::setRoot(const QString &contentDir)
{
    if (m_root == contentDir)
        return;
    m_root = contentDir;
    clear();
    emit changed();
}

void ModelCache::clear()
{
    m_entries.clear();
}

const ModelAsset *ModelCache::find(const QString &path)
{
    const QString trimmed = path.trimmed();
    if (trimmed.isEmpty())
        return nullptr;
    const QFileInfo info(QDir::isAbsolutePath(trimmed) ? trimmed : QDir(m_root).absoluteFilePath(trimmed));
    const QString resolved = QDir::cleanPath(info.absoluteFilePath());
    const QDateTime modified = info.exists() ? info.lastModified() : QDateTime();

    auto found = m_entries.find(resolved);
    if (found != m_entries.end() && found->modified == modified)
        return found->asset.get();

    Entry entry;
    entry.modified = modified;
    if (info.suffix().compare("kmodel", Qt::CaseInsensitive) != 0)
    {
        emit message(tr("Model preview supports .kmodel only: %1").arg(trimmed), kWarning);
    }
    else if (!info.exists())
    {
        emit message(tr("Model not found: %1").arg(trimmed), kWarning);
    }
    else
    {
        KModelData data;
        MeshData mesh;
        std::string error;
        const QByteArray file = resolved.toUtf8();
        if (!LoadKModelFile(file.constData(), data, &error) || !BuildMeshDataFromKModel(data, mesh, &error))
        {
            emit message(tr("Could not load model %1: %2").arg(trimmed, QString::fromStdString(error)), kWarning);
        }
        else
        {
            auto asset = std::make_shared<ModelAsset>();
            asset->vertices.reserve(int(mesh.vertices.size()));
            for (const MeshVertex &v : mesh.vertices)
            {
                ModelVertex out;
                out.position = QVector3D(v.px, v.py, v.pz);
                out.normal = QVector3D(v.nx, v.ny, v.nz);
                out.u = v.u;
                out.v = v.v;
                out.r = v.r;
                out.g = v.g;
                out.b = v.b;
                out.a = v.a;
                asset->bounds.expand(out.position);
                asset->vertices.push_back(out);
            }
            asset->indices.reserve(int(mesh.indices.size()));
            for (uint32_t index : mesh.indices)
            {
                if (index < mesh.vertices.size())
                    asset->indices.push_back(index);
            }
            asset->indices.resize(asset->indices.size() - asset->indices.size() % 3);
            entry.asset = std::move(asset);
        }
    }
    found = m_entries.insert(resolved, entry);
    return found->asset.get();
}

QMatrix4x4 ModelCache::placement(const MapObject &o)
{
    // Mirrors the engine: ConvertMapAngles (map_file.cpp), TransformToMatrix (render_common.cpp,
    // scale then X, Y, Z rotations), then engine (x, y, z) -> editor (x, z, y) / unit scale.
    const float k = 1.f / ENGINE_MAP_UNIT_SCALE;
    const QMatrix4x4 engineToEditor(k, 0.f, 0.f, 0.f,
                                    0.f, 0.f, k, 0.f,
                                    0.f, k, 0.f, 0.f,
                                    0.f, 0.f, 0.f, 1.f);
    QMatrix4x4 m;
    m.translate(o.origin);
    m *= engineToEditor;
    m.rotate(o.angles.z(), 0.f, 0.f, 1.f);
    m.rotate(90.f - o.angles.y(), 0.f, 1.f, 0.f);
    m.rotate(-o.angles.x(), 1.f, 0.f, 0.f);
    m.scale(parseScale(objectValue(o, "Scale", "1 1 1")));
    return m;
}

AABB ModelCache::worldBounds(const ModelAsset &model, const QMatrix4x4 &placement)
{
    AABB out;
    if (!model.bounds.valid())
        return out;
    for (int i = 0; i < 8; ++i)
    {
        const QVector3D corner(i & 1 ? model.bounds.mx.x() : model.bounds.mn.x(),
                               i & 2 ? model.bounds.mx.y() : model.bounds.mn.y(),
                               i & 4 ? model.bounds.mx.z() : model.bounds.mn.z());
        out.expand(placement.map(corner));
    }
    return out;
}
