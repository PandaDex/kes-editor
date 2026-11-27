// Dragging assets into a viewport: materials preview on the brush under the cursor and revert when
// the drag leaves; particle systems become entities placed under the cursor. One undo step each.
#include "core/EditorContext.h"
#include "render/Viewport.h"
#include "tools/AssetDrop.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QMimeData>
#include <QSettings>
#include <QTemporaryDir>
#include <QUrl>
#include <cstdio>
#include <memory>

static int g_failures = 0;
#define EXPECT(cond)                                                       \
    do                                                                     \
    {                                                                      \
        if (!(cond))                                                       \
        {                                                                  \
            std::printf("FAILED %s:%d: %s\n", __FILE__, __LINE__, #cond);  \
            ++g_failures;                                                  \
        }                                                                  \
    } while (0)

static void writeFile(const QString& path, const QByteArray& data)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return;
    f.write(data);
}

static std::unique_ptr<QMimeData> mimeFor(const QString& path)
{
    auto mime = std::make_unique<QMimeData>();
    mime->setUrls({ QUrl::fromLocalFile(path) });
    return mime;
}

static QString faceMaterial(const MapObject* o)
{
    return o && !o->brushes.isEmpty() && !o->brushes.first().faces.isEmpty() ? o->brushes.first().faces.first().material : QString("<none>");
}

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    QApplication::setOrganizationName("kes-tests");
    QApplication::setApplicationName("assetdrop_test");
    QSettings().clear();

    QTemporaryDir dir;
    const QString root = dir.path();
    writeFile(root + "/materials/brick.mat", "Material {\n    BaseColor = \"1,0,0,1\"\n}\n");
    writeFile(root + "/particles/fire.kfx", "{}\n");

    EditorContext ctx;
    ctx.setProjectDir(root);
    MapDocument* doc = ctx.doc();

    Viewport view(&ctx, ViewKind::Top);
    Camera& cam = view.camera();
    cam.size = QSize(400, 400);
    cam.center = QVector2D(0, 0);
    cam.zoom = 1.f;
    cam.update();

    MapObject box;
    box.solid = true;
    box.ensureBrushes();
    box.setWorldBounds(AABB(QVector3D(-32, -32, 0), QVector3D(32, 32, 64)));
    const ObjectId boxId = doc->addObject(box);
    const int baseUndo = doc->undoStack()->count();

    QPointF onBox;
    cam.project(QVector3D(0, 0, 0), onBox);
    const auto material = mimeFor(root + "/materials/brick.mat");

    {
        std::unique_ptr<AssetDrop> drop(AssetDrop::create(&ctx, &view, material.get()));
        EXPECT(drop != nullptr);
        if (drop)
        {
            drop->move(onBox, false);
            EXPECT(faceMaterial(doc->find(boxId)) == "materials/brick.mat");
            EXPECT(doc->undoStack()->count() == baseUndo);
            drop->move(onBox + QPointF(150, 150), false); // off the box: preview reverts
            EXPECT(faceMaterial(doc->find(boxId)).isEmpty());
            drop->move(onBox, false);
        }
    } // dropped object destroyed without commit = drag left the view
    EXPECT(faceMaterial(doc->find(boxId)).isEmpty());
    EXPECT(doc->undoStack()->count() == baseUndo);

    {
        std::unique_ptr<AssetDrop> drop(AssetDrop::create(&ctx, &view, material.get()));
        drop->move(onBox, false);
        drop->commit(onBox, false);
    }
    EXPECT(faceMaterial(doc->find(boxId)) == "materials/brick.mat");
    EXPECT(doc->undoStack()->count() == baseUndo + 1);
    EXPECT(ctx.currentMaterial() == "materials/brick.mat");
    doc->undoStack()->undo();
    EXPECT(faceMaterial(doc->find(boxId)).isEmpty());

    const auto fire = mimeFor(root + "/particles/fire.kfx");
    const size_t objectsBefore = doc->objects().size();
    QPointF at;
    cam.project(QVector3D(130, 70, 0), at);
    {
        std::unique_ptr<AssetDrop> drop(AssetDrop::create(&ctx, &view, fire.get()));
        EXPECT(drop != nullptr);
        if (drop)
        {
            drop->move(at, false);
            EXPECT(doc->objects().size() == objectsBefore + 1); // live preview
        }
    }
    EXPECT(doc->objects().size() == objectsBefore);

    const int undoBefore = doc->undoStack()->index();
    {
        std::unique_ptr<AssetDrop> drop(AssetDrop::create(&ctx, &view, fire.get()));
        drop->move(at, false);
        drop->commit(at, false);
    }
    EXPECT(doc->objects().size() == objectsBefore + 1);
    EXPECT(doc->undoStack()->index() == undoBefore + 1);
    EXPECT(doc->selection().size() == 1);
    const MapObject* placed = doc->selection().isEmpty() ? nullptr : doc->find(*doc->selection().begin());
    EXPECT(placed && placed->classname == "ParticleSystemEntity");
    EXPECT(placed && placed->kv.value("System") == "particles/fire.kfx");
    EXPECT(placed && placed->name == "fire_1");
    if (placed)
        std::printf("placed at %g %g %g\n", placed->origin.x(), placed->origin.y(), placed->origin.z());
    // grid 16 by default: 130,70 snaps to 128,64
    EXPECT(placed && placed->origin == QVector3D(128, 64, 0));

    // 3D: a real model stands on the floor plane, then takes a material on the part under the cursor
    const QString planeSource = QStringLiteral(KES_GAME_CONTENT "/meshes/plane.kmodel");
    if (QFile::exists(planeSource))
    {
        QDir().mkpath(root + "/meshes");
        QFile::copy(planeSource, root + "/meshes/plane.kmodel");
        doc->deleteObjects({ boxId });
        Viewport persp(&ctx, ViewKind::Perspective);
        Camera& pc = persp.camera();
        pc.size = QSize(400, 400);
        pc.pos = QVector3D(0, -300, 300);
        pc.yaw = 90.f;
        pc.pitch = -45.f;
        pc.update();
        QPointF centre;
        pc.project(QVector3D(0, 0, 0), centre);

        const auto model = mimeFor(root + "/meshes/plane.kmodel");
        {
            std::unique_ptr<AssetDrop> drop(AssetDrop::create(&ctx, &persp, model.get()));
            EXPECT(drop != nullptr);
            if (drop)
                drop->commit(centre, false);
        }
        const MapObject* mesh = doc->selection().isEmpty() ? nullptr : doc->find(*doc->selection().begin());
        EXPECT(mesh && (mesh->classname == "StaticMeshEntity" || mesh->classname == "SkinnedModelEntity"));
        EXPECT(mesh && mesh->kv.value("Model") == "meshes/plane.kmodel");
        if (mesh)
            std::printf("model placed at %g %g %g\n", mesh->origin.x(), mesh->origin.y(), mesh->origin.z());
        EXPECT(mesh && std::abs(mesh->origin.x()) < 2.f && std::abs(mesh->origin.y()) < 2.f);

        if (mesh)
        {
            const ObjectId meshId = mesh->id;
            QPointF onModel;
            pc.project(mesh->origin, onModel);
            std::unique_ptr<AssetDrop> drop(AssetDrop::create(&ctx, &persp, material.get()));
            drop->commit(onModel, false);
            const MapObject* painted = doc->find(meshId);
            bool hasMaterial = false;
            for (auto it = painted->kv.cbegin(); it != painted->kv.cend(); ++it)
                if (it.key().startsWith("Material", Qt::CaseInsensitive) && it.value() == "materials/brick.mat")
                {
                    std::printf("model material key %s\n", qPrintable(it.key()));
                    hasMaterial = true;
                }
            EXPECT(hasMaterial);
        }
    }
    else
    {
        std::printf("skipped 3D model drop: %s missing\n", qPrintable(planeSource));
    }

    QSettings().clear();
    std::printf(g_failures ? "%d failure(s)\n" : "assetdrop_test passed\n", g_failures);
    return g_failures ? 1 : 0;
}
