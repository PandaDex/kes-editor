// Asset Browser: folder colours (persist, follow moves, drop on delete), back/forward history,
// path bar segments and editing, texture thumbnails. Optional argv[1]: save a render of the browser.
#include "assets/AssetImporter.h"
#include "core/EditorContext.h"
#include "ui/AssetBrowser.h"
#include "ui/AssetPathBar.h"
#include "ui/AssetTiles.h"
#include "ui/FolderColors.h"
#include "ui/KesStyle.h"

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QImage>
#include <QLineEdit>
#include <QListView>
#include <QSettings>
#include <QTemporaryDir>
#include <QToolButton>
#include <QResizeEvent>
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

static void wait(QApplication& app, int ms)
{
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < ms)
        app.processEvents(QEventLoop::AllEvents, 10);
}

static QStringList visibleCrumbs(AssetPathBar* bar)
{
    QStringList out;
    for (QToolButton* b : bar->findChildren<QToolButton*>("Crumb"))
        if (b->isVisibleTo(bar) && b->text() != QStringLiteral("…"))
            out << b->text();
    return out;
}

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    QApplication::setOrganizationName("kes-tests");
    QApplication::setApplicationName("assetbrowser_test");
    QSettings().clear();
    kes::style::apply(app);

    QTemporaryDir dir;
    const QString content = dir.filePath("content");
    for (const char* sub : { "maps", "textures/env", "textures/props", "ui" })
        QDir().mkpath(QDir(content).filePath(sub));
    QImage red(32, 32, QImage::Format_RGB32);
    red.fill(Qt::red);
    EXPECT(red.save(QDir(content).filePath("textures/brick.png")));
    QFile(QDir(content).filePath("maps/start.fmap")).open(QIODevice::WriteOnly);

    // FolderColors on its own
    {
        FolderColors colors;
        colors.setProject("projA", content);
        const QString env = QDir(content).filePath("textures/env");
        colors.setColor(env, QColor("#5a9cf5"));
        colors.setColor(QDir(content).filePath("textures"), QColor("#5cc26e"));
        EXPECT(colors.color(env) == QColor("#5a9cf5"));
        EXPECT(!colors.color(QDir(content).filePath("maps")).isValid());

        FolderColors reloaded;
        reloaded.setProject("projA", content);
        EXPECT(reloaded.color(env) == QColor("#5a9cf5"));
        FolderColors other;
        other.setProject("projB", content);
        EXPECT(!other.color(env).isValid());

        reloaded.moved(QDir(content).filePath("textures"), QDir(content).filePath("art"));
        EXPECT(reloaded.color(QDir(content).filePath("art/env")) == QColor("#5a9cf5"));
        EXPECT(reloaded.color(QDir(content).filePath("art")) == QColor("#5cc26e"));
        EXPECT(!reloaded.color(env).isValid());
        reloaded.removed(QDir(content).filePath("art"));
        EXPECT(!reloaded.color(QDir(content).filePath("art/env")).isValid());
        reloaded.setProject("projA", content);
        EXPECT(!reloaded.color(QDir(content).filePath("art")).isValid());
    }

    // Thumbnails decode in the background
    {
        ThumbnailCache cache;
        bool ready = false;
        QObject::connect(&cache, &ThumbnailCache::ready, [&ready] { ready = true; });
        const QString png = QDir(content).filePath("textures/brick.png");
        EXPECT(cache.thumbnail(png).isNull());
        QElapsedTimer t;
        t.start();
        while (!ready && t.elapsed() < 3000)
            app.processEvents(QEventLoop::AllEvents, 10);
        EXPECT(ready);
        EXPECT(!cache.thumbnail(png).isNull());
    }

    EditorContext ctx;
    ctx.setProjectDir(content);
    AssetImporter importer(&ctx, nullptr);
    AssetBrowser browser(&ctx, &importer);
    browser.resize(1100, 420);
    // laid out and painted like a real window, but never put on screen
    browser.setAttribute(Qt::WA_DontShowOnScreen);
    browser.show();
    auto* bar = browser.findChild<AssetPathBar*>();
    EXPECT(bar);
    wait(app, 300);

    // Path bar follows navigation; back/forward walk the history
    const auto crumbs = [&] { return visibleCrumbs(bar); };
    EXPECT(crumbs() == QStringList{ "content" });
    emit bar->navigate(QDir(content).filePath("textures"));
    emit bar->navigate(QDir(content).filePath("textures/env"));
    EXPECT((crumbs() == QStringList{ "content", "textures", "env" }));
    auto* back = browser.findChild<QToolButton*>("BackButton");
    auto* forward = browser.findChild<QToolButton*>("ForwardButton");
    EXPECT(back && forward && back->isEnabled() && !forward->isEnabled());
    back->click();
    EXPECT((crumbs() == QStringList{ "content", "textures" }));
    back->click();
    EXPECT(crumbs() == QStringList{ "content" });
    EXPECT(!back->isEnabled());
    forward->click();
    EXPECT((crumbs() == QStringList{ "content", "textures" }));
    // navigating from the middle of the history drops the forward part
    emit bar->navigate(QDir(content).filePath("ui"));
    EXPECT(!forward->isEnabled());
    EXPECT((crumbs() == QStringList{ "content", "ui" }));
    back->click();
    EXPECT((crumbs() == QStringList{ "content", "textures" }));

    // Typed paths
    {
        auto* edit = bar->findChild<QLineEdit*>("PathEdit");
        EXPECT(edit);
        edit->setText("content/textures/props");
        emit edit->returnPressed();
        EXPECT((crumbs() == QStringList{ "content", "textures", "props" }));
        edit->setText("nowhere");
        emit edit->returnPressed();
        EXPECT(edit->property("invalid").toBool());
    }

    // Narrow bars collapse leading segments but keep the current folder
    browser.resize(380, 420);
    wait(app, 100);
    EXPECT(!crumbs().isEmpty() && crumbs().last() == "props");
    EXPECT(crumbs().size() < 3);
    browser.resize(1100, 420);
    wait(app, 100);
    EXPECT((crumbs() == QStringList{ "content", "textures", "props" }));

    // Grid shows the texture's thumbnail once it is decoded
    emit bar->navigate(QDir(content).filePath("textures"));
    wait(app, 600);
    auto* grid = browser.findChild<QListView*>("AssetGrid");
    EXPECT(grid);
    bool previewed = false;
    for (int r = 0; grid && r < grid->model()->rowCount(grid->rootIndex()); ++r)
    {
        const QModelIndex i = grid->model()->index(r, 0, grid->rootIndex());
        if (i.data().toString() == "brick.png")
            previewed = !i.data(AssetRoles::Preview).value<QIcon>().isNull();
    }
    EXPECT(previewed);

    if (argc > 1)
    {
        FolderColors* colors = browser.findChild<FolderColors*>();
        colors->setColor(QDir(content).filePath("textures/env"), QColor("#5a9cf5"));
        colors->setColor(QDir(content).filePath("maps"), QColor("#e55a5a"));
        emit bar->navigate(QDir(content).filePath("textures"));
        wait(app, 600);
        browser.grab().save(QString::fromLocal8Bit(argv[1]));
    }

    QSettings().clear();
    std::printf(g_failures ? "%d failures\n" : "all passed\n", g_failures);
    return g_failures ? 1 : 0;
}
