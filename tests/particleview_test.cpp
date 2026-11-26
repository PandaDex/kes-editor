// Particle preview: every .kfx given on the command line (or the game's) opens in the tab and renders for a while.
#include "core/EditorContext.h"
#include "modules/particles/ParticleTab.h"
#include "modules/particles/ParticleViewport.h"

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QSurfaceFormat>
#include <cstdio>

int main(int argc, char** argv)
{
    QSurfaceFormat fmt;
    fmt.setVersion(4, 5);
    fmt.setProfile(QSurfaceFormat::CoreProfile);
    fmt.setDepthBufferSize(24);
    QSurfaceFormat::setDefaultFormat(fmt);
    QApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
    QApplication app(argc, argv);
    EditorContext ctx;

    QStringList files;
    for (int i = 1; i < argc; ++i)
        files << QString::fromLocal8Bit(argv[i]);
    if (files.isEmpty())
    {
        const QDir dir(QStringLiteral(FORGE_SOURCE_DIR "/../../game/content/particles"));
        for (const QString& f : dir.entryList({ "*.kfx" }))
            files << dir.filePath(f);
    }
    int failures = 0;
    for (const QString& file : files)
    {
        std::printf("%s\n", qPrintable(file));
        std::fflush(stdout);
        kpe::ParticleTab tab(&ctx);
        QString error;
        if (!tab.openFile(file, &error))
        {
            std::printf("  open failed: %s\n", qPrintable(error));
            ++failures;
            continue;
        }
        auto* view = tab.findChild<kpe::ParticleViewport*>();
        if (!view)
            continue;
        view->resize(480, 320);
        QElapsedTimer timer;
        timer.start();
        int frames = 0;
        while (timer.elapsed() < 2500)
        {
            app.processEvents(QEventLoop::AllEvents, 16);
            view->grabFramebuffer();
            ++frames;
        }
        std::printf("  %d frames, errors: %s\n", frames, qPrintable(view->errors().join("; ")));

        // Toolbar Pause / Play (it used to call an empty slot and abort)
        QAction* pause = nullptr;
        for (QAction* a : tab.findChildren<QAction*>())
            if (a->text() == "Pause")
                pause = a;
        if (!pause)
        {
            std::printf("  no Pause action\n");
            ++failures;
        }
        else
        {
            pause->trigger();
            const bool paused = !view->isPlaying();
            app.processEvents();
            pause->trigger();
            if (!paused || !view->isPlaying())
            {
                std::printf("  pause/play did not toggle\n");
                ++failures;
            }
        }
        std::fflush(stdout);
    }
    std::printf(failures ? "%d failures\n" : "all passed\n", failures);
    return failures ? 1 : 0;
}
