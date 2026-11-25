// .kproject handling: EntityDefPaths folders feed the entity registry.
#include "core/EditorContext.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QSettings>
#include <QTemporaryDir>
#include <cstdio>

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

static void writeFile(const QString& path, const QByteArray& text)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    f.open(QIODevice::WriteOnly | QIODevice::Truncate);
    f.write(text);
}

static QByteArray pointClass(const char* name, const char* description)
{
    return QByteArray("@PointClass size(-8 -8 -8, 8 8 8) = ") + name + " : \"" + description + "\"\n[\n]\n";
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    // keep the user's Forge settings (last project etc.) out of the test
    QCoreApplication::setOrganizationName("kes-tests");
    QCoreApplication::setApplicationName("forge_project_test");

    QTemporaryDir tmp;
    const QDir root(tmp.path());
    QDir().mkpath(root.filePath("a/content"));
    QDir().mkpath(root.filePath("b/content"));
    writeFile(root.filePath("a/a.kproject"), "Project {\n    Name=\"A\"\n    AssetRoot=\"content\"\n}\n");
    writeFile(root.filePath("a/editor/game.dec"), pointClass("GameOnlyEntity", "from editor/") + pointClass("LightEntity", "project light"));
    writeFile(root.filePath("b/b.kproject"), "Project {\n    Name=\"B\"\n    AssetRoot=\"content\"\n    EntityDefPaths=\"defs/one; defs/two\"\n}\n");
    writeFile(root.filePath("b/defs/one/one.dec"), pointClass("FirstEntity", "one"));
    writeFile(root.filePath("b/defs/two/two.dec"), pointClass("SecondEntity", "two"));
    writeFile(root.filePath("b/editor/ignored.dec"), pointClass("IgnoredEntity", "editor/ is not listed"));

    EditorContext ctx;
    EXPECT(ctx.registry()->find("LightEntity") != nullptr); // Forge's own entities/kes.dec
    EXPECT(ctx.registry()->find("GameOnlyEntity") == nullptr);

    // default folder: editor/ next to the .kproject; project classes override engine ones
    QString error;
    EXPECT(ctx.loadProject(root.filePath("a/a.kproject"), &error, false));
    EXPECT(ctx.project().entityDefPathsValue == "editor");
    EXPECT(ctx.registry()->find("GameOnlyEntity") != nullptr);
    const ClassDef* light = ctx.registry()->find("LightEntity");
    EXPECT(light && light->description == "project light");

    // explicit list replaces the default, and the previous project's classes go away
    EXPECT(ctx.loadProject(root.filePath("b/b.kproject"), &error, false));
    EXPECT(ctx.project().entityDefDirs.size() == 2);
    EXPECT(ctx.registry()->find("FirstEntity") != nullptr);
    EXPECT(ctx.registry()->find("SecondEntity") != nullptr);
    EXPECT(ctx.registry()->find("IgnoredEntity") == nullptr);
    EXPECT(ctx.registry()->find("GameOnlyEntity") == nullptr);
    light = ctx.registry()->find("LightEntity");
    EXPECT(light && light->description != "project light");

    // a bare content folder has no project folders
    ctx.setProjectDir(root.filePath("a/content"));
    EXPECT(ctx.registry()->find("FirstEntity") == nullptr);

    // groups: engine classes are in "Engine"; @Group defaults, group() overrides, bases pass it on
    light = ctx.registry()->resolved("LightEntity");
    EXPECT(light && light->group == "Engine");
    {
        writeFile(root.filePath("groups.dec"),
                  "@BaseClass group(\"Game/Props\") = PropBase\n[\n]\n"
                  "@PointClass = Ungrouped : \"\"\n[\n]\n"
                  "@Group \"Game\"\n"
                  "@PointClass = DefaultGrouped : \"\"\n[\n]\n"
                  "@PointClass group(\"Game/AI\") = Explicit : \"\"\n[\n]\n"
                  "@PointClass base(PropBase) = Crate : \"\"\n[\n]\n");
        EntityRegistry reg;
        QStringList errors;
        EXPECT(reg.loadFile(root.filePath("groups.dec"), &errors));
        EXPECT(errors.isEmpty());
        const ClassDef* c = reg.resolved("Ungrouped");
        EXPECT(c && c->group.isEmpty());
        c = reg.resolved("DefaultGrouped");
        EXPECT(c && c->group == "Game");
        c = reg.resolved("Explicit");
        EXPECT(c && c->group == "Game/AI");
        c = reg.resolved("Crate"); // explicit group on the base beats the file default
        EXPECT(c && c->group == "Game/Props");
        EXPECT(EntityRegistry::toDec(*reg.find("Explicit")).contains("group(\"Game/AI\")"));
    }

    // settings and the user entities folder the context created for this test app
    QSettings().clear();
    QDir(QFileInfo(ctx.userEntityDir()).absolutePath()).removeRecursively();

    if (g_failures == 0) std::printf("all project checks passed\n");
    return g_failures == 0 ? 0 : 1;
}
