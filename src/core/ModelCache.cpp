#include "core/ModelCache.h"
#include "core/MapObject.h"

#include "animation_pose.h"
#include "engine_configuration.h"
#include "kmodel.h"

#include <QDir>
#include <QFileInfo>
#include <algorithm>
#include <functional>

namespace
{
constexpr int kWarning = 1;

QDateTime fileTime(const QString &path)
{
    const QFileInfo info(path);
    return info.exists() ? info.lastModified() : QDateTime();
}

ModelVertex toVertex(const MeshVertex &v)
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
    return out;
}

QVector3D mapPoint(const float *m, const QVector3D &p)
{
    return {p.x() * m[0] + p.y() * m[4] + p.z() * m[8] + m[12], p.x() * m[1] + p.y() * m[5] + p.z() * m[9] + m[13],
            p.x() * m[2] + p.y() * m[6] + p.z() * m[10] + m[14]};
}

QVector3D mapDirection(const float *m, const QVector3D &d)
{
    return QVector3D(d.x() * m[0] + d.y() * m[4] + d.z() * m[8], d.x() * m[1] + d.y() * m[5] + d.z() * m[9],
                     d.x() * m[2] + d.y() * m[6] + d.z() * m[10])
        .normalized();
}

// Copies the mesh's in-range whole triangles, keeping each section's range valid in the copy.
void copySections(const MeshData &mesh, QVector<quint32> &indices, QVector<ModelSection> &sections)
{
    std::vector<MeshSection> source = mesh.sections;
    if (source.empty())
        source.push_back({0, uint32_t(mesh.indices.size()), -1});
    for (const MeshSection &s : source)
    {
        ModelSection out;
        out.firstIndex = int(indices.size());
        out.slot = s.materialSlot;
        const size_t end = std::min<size_t>(size_t(s.firstIndex) + s.indexCount, mesh.indices.size());
        for (size_t i = s.firstIndex; i + 2 < end; i += 3)
        {
            const uint32_t a = mesh.indices[i], b = mesh.indices[i + 1], c = mesh.indices[i + 2];
            if (a >= mesh.vertices.size() || b >= mesh.vertices.size() || c >= mesh.vertices.size())
                continue;
            indices << a << b << c;
        }
        out.indexCount = int(indices.size()) - out.firstIndex;
        if (out.indexCount > 0)
            sections.push_back(out);
    }
}
}

QString ModelCache::resolve(const QString &path) const
{
    const QString trimmed = path.trimmed();
    return QDir::cleanPath(QFileInfo(QDir::isAbsolutePath(trimmed) ? trimmed : QDir(m_root).absoluteFilePath(trimmed)).absoluteFilePath());
}

namespace
{
// same depth limit as SkinnedModelEntity::CreateChildParts
constexpr int kMaxChildDepth = 4;

struct ChildBaker
{
    std::function<QString(const QString &)> resolve;
    QVector<QPair<QString, QDateTime>> dependencies;
    QStringList errors;

    void bake(const KModelData &parent, const std::vector<float> &parentWorld, const float *parentMatrix, int depth,
              ModelAsset &out)
    {
        if (depth >= kMaxChildDepth)
            return;
        for (const KModelChild &child : parent.children)
        {
            const QString path = QString::fromStdString(child.modelPath);
            if (path.trimmed().isEmpty())
                continue;
            const QString file = resolve(path);
            dependencies.push_back({file, fileTime(file)});

            KModelData data;
            MeshData mesh;
            std::string error;
            if (!LoadKModelFile(file.toUtf8().constData(), data, &error) || !BuildMeshDataFromKModel(data, mesh, &error))
            {
                errors << QStringLiteral("%1: %2").arg(path, QString::fromStdString(error));
                continue;
            }

            float local[16], matrix[16];
            BuildKModelChildMatrix(parent, parentWorld, child, local);
            if (parentMatrix)
                MultiplyMatrices(local, parentMatrix, matrix);
            else
                std::copy(local, local + 16, matrix);

            // Same as the engine: slot material, then the child's Material, then the child model's default.
            ModelChildPart part;
            const QString fallback = QString::fromStdString(child.materialPath.empty() ? data.defaultMaterial : child.materialPath);
            part.vertices.reserve(int(mesh.vertices.size()));
            for (const MeshVertex &v : mesh.vertices)
            {
                ModelVertex vertex = toVertex(v);
                vertex.position = mapPoint(matrix, vertex.position);
                vertex.normal = mapDirection(matrix, vertex.normal);
                out.bounds.expand(vertex.position);
                part.vertices.push_back(vertex);
            }
            copySections(mesh, part.indices, part.sections);
            for (ModelSection &section : part.sections)
            {
                const bool hasSlot = section.slot >= 0 && size_t(section.slot) < data.materials.size() &&
                                     !data.materials[size_t(section.slot)].materialPath.empty();
                section.material = hasSlot ? QString::fromStdString(data.materials[size_t(section.slot)].materialPath) : fallback;
            }
            out.children.push_back(std::move(part));

            std::vector<float> world;
            BuildBindPoseWorldMatrices(data, world);
            bake(data, world, matrix, depth + 1, out);
        }
    }
};
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
    const QString resolved = resolve(trimmed);
    const QFileInfo info(resolved);
    const QDateTime modified = fileTime(resolved);

    auto found = m_entries.find(resolved);
    if (found != m_entries.end() && found->modified == modified &&
        std::all_of(found->dependencies.cbegin(), found->dependencies.cend(),
                    [](const QPair<QString, QDateTime> &d) { return fileTime(d.first) == d.second; }))
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
            asset->defaultMaterial = QString::fromStdString(data.defaultMaterial);
            asset->vertices.reserve(int(mesh.vertices.size()));
            for (const MeshVertex &v : mesh.vertices)
            {
                const ModelVertex out = toVertex(v);
                asset->bounds.expand(out.position);
                asset->vertices.push_back(out);
            }
            asset->indices.reserve(int(mesh.indices.size()));
            copySections(mesh, asset->indices, asset->sections);
            for (const KModelMaterial &slot : data.materials)
                asset->materialSlots.push_back({QString::fromStdString(slot.name), QString::fromStdString(slot.materialPath)});

            ChildBaker baker;
            baker.resolve = [this](const QString &p) { return resolve(p); };
            std::vector<float> world;
            BuildBindPoseWorldMatrices(data, world);
            baker.bake(data, world, nullptr, 0, *asset);
            for (const QString &e : baker.errors)
                emit message(tr("Model %1: child model %2").arg(trimmed, e), kWarning);
            entry.dependencies = std::move(baker.dependencies);
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
