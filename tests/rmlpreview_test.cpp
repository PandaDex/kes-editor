// UI preview: a sample document resolves its style sheets and fonts, renders, and reloads on change.
#include "core/EditorContext.h"
#include "modules/rmlPreview/RmlPreviewTab.h"
#include "modules/rmlPreview/RmlPreviewView.h"

#include <QApplication>
#include <QDataStream>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QSurfaceFormat>
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

static void printProblems(const EditorTab& tab)
{
    for (const Problem& p : tab.problems())
        std::printf("  problem: %s\n", qPrintable(p.message));
}

static int distinctColors(const QImage& image)
{
    QSet<QRgb> colors;
    for (int y = 0; y < image.height(); y += 4)
        for (int x = 0; x < image.width(); x += 4)
            colors.insert(image.pixel(x, y));
    return int(colors.size());
}

int main(int argc, char** argv)
{
    QSurfaceFormat fmt;
    fmt.setVersion(4, 5);
    fmt.setProfile(QSurfaceFormat::CoreProfile);
    QSurfaceFormat::setDefaultFormat(fmt);
    QApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
    QApplication app(argc, argv);
    EditorContext ctx;

    const QString sample = QDir::cleanPath(QStringLiteral(FORGE_SOURCE_DIR "/../../samples/ui/content/ui/settings.rml"));
    {
        rmlp::RmlPreviewTab tab(&ctx);
        QString error;
        EXPECT(tab.openFile(sample, &error));
        printProblems(tab);
        for (const Problem& p : tab.problems())
        {
            EXPECT(!p.message.contains("Could not load"));
            EXPECT(!p.message.contains("font", Qt::CaseInsensitive));
        }
        auto* view = tab.findChild<rmlp::RmlPreviewView*>();
        EXPECT(view);
        if (view)
        {
            view->resize(960, 540);
            const QImage frame = view->grabFramebuffer();
            std::printf("frame %dx%d, %d colors\n", frame.width(), frame.height(), distinctColors(frame));
            EXPECT(!frame.isNull());
            if (argc > 1)
                frame.save(QString::fromLocal8Bit(argv[1]));
            EXPECT(distinctColors(frame) > 20);
        }
    }

    // Live reload and #Key translation in a document of its own
    QTemporaryDir dir;
    QDir(dir.path()).mkpath("content/ui");
    QDir(dir.path()).mkpath("content/localization");
    const QString doc = dir.filePath("content/ui/test.rml");
    const auto write = [](const QString& path, const QByteArray& text)
    {
        QFile f(path);
        f.open(QIODevice::WriteOnly | QIODevice::Truncate);
        f.write(text);
    };
    write(dir.filePath("content/localization/en.po"), "msgid \"\"\nmsgstr \"\"\n\"Language: en\\n\"\n\nmsgid \"Test.Title\"\nmsgstr \"Hello\"\n");
    write(doc, "<rml><head><link type=\"text/rcss\" href=\"/engine/ui/kes.rcss\"/></head><body><div id=\"t\">#Test.Title</div></body></rml>");
    {
        rmlp::RmlPreviewTab tab(&ctx);
        QString error;
        EXPECT(tab.openFile(doc, &error));
        printProblems(tab);
        EXPECT(tab.problems().isEmpty());

        write(doc, "<rml><head><link type=\"text/rcss\" href=\"missing.rcss\"/></head><body>x</body></rml>");
        QElapsedTimer timer;
        timer.start();
        while (tab.problems().isEmpty() && timer.elapsed() < 5000)
            app.processEvents(QEventLoop::AllEvents, 50);
        printProblems(tab);
        EXPECT(!tab.problems().isEmpty());
    }

    // Images next to the document (relative src) and through a style sheet decorator
    {
        QDir(dir.path()).mkpath("content/ui/icons");
        QImage icon(8, 8, QImage::Format_RGB32);
        icon.fill(Qt::red);
        EXPECT(icon.save(dir.filePath("content/ui/icons/a.bmp")));
        write(doc, "<rml><head><link type=\"text/rcss\" href=\"/engine/ui/kes.rcss\"/><style>#d { width: 32px; height: 32px; decorator: image(icons/a.bmp); }</style></head>"
                   "<body><img src=\"icons/a.bmp\" style=\"width: 64px; height: 64px;\"/><div id=\"d\"/></body></rml>");
        rmlp::RmlPreviewTab tab(&ctx);
        QString error;
        EXPECT(tab.openFile(doc, &error));
        auto* view = tab.findChild<rmlp::RmlPreviewView*>();
        view->resize(640, 360);
        const QImage frame = view->grabFramebuffer();
        printProblems(tab);
        EXPECT(tab.problems().isEmpty());
        int red = 0;
        for (int y = 0; y < frame.height(); ++y)
            for (int x = 0; x < frame.width(); ++x)
                if (QColor(frame.pixel(x, y)).red() > 200 && QColor(frame.pixel(x, y)).green() < 60)
                    ++red;
        std::printf("bmp red pixels: %d\n", red);
        EXPECT(red > 300); // 64 px at up to 1920 wide scaled into 640
    }

    // .ktex (RGBA8) next to the document, from <img> and from a decorator
    {
        QDir(dir.path()).mkpath("content/ui/icons");
        QByteArray ktex;
        QDataStream out(&ktex, QIODevice::WriteOnly);
        out.setByteOrder(QDataStream::LittleEndian);
        out << quint32(0x5845544Bu) << quint32(1) << quint32(0)
            << quint32(8) << quint32(8) << quint32(1) << quint32(0) << quint32(1) << quint32(8 * 8 * 4);
        for (int i = 0; i < 64; ++i)
            out << quint8(255) << quint8(0) << quint8(0) << quint8(255);
        write(dir.filePath("content/ui/icons/red.ktex"), ktex);
        write(doc, "<rml><head><link type=\"text/rcss\" href=\"/engine/ui/kes.rcss\"/>"
                   "<style>#d { width: 64px; height: 64px; decorator: image(icons/red.ktex); }</style></head>"
                   "<body><img src=\"icons/red.ktex\" style=\"width: 64px; height: 64px;\"/><div id=\"d\"/></body></rml>");
        rmlp::RmlPreviewTab tab(&ctx);
        QString error;
        EXPECT(tab.openFile(doc, &error));
        auto* view = tab.findChild<rmlp::RmlPreviewView*>();
        view->resize(640, 360);
        const QImage frame = view->grabFramebuffer();
        printProblems(tab);
        EXPECT(tab.problems().isEmpty());
        int red = 0;
        for (int y = 0; y < frame.height(); ++y)
            for (int x = 0; x < frame.width(); ++x)
            {
                const QColor c(frame.pixel(x, y));
                if (c.red() > 200 && c.green() < 60 && c.blue() < 60)
                    ++red;
            }
        std::printf("ktex red pixels: %d\n", red);
        EXPECT(red > 600); // two images
    }

    // Extra document to check by hand: RMLPREVIEW_DOC=<path> [frame.png as argv[1]]
    if (const QString extra = qEnvironmentVariable("RMLPREVIEW_DOC"); !extra.isEmpty())
    {
        rmlp::RmlPreviewTab tab(&ctx);
        QString error;
        EXPECT(tab.openFile(extra, &error));
        auto* view = tab.findChild<rmlp::RmlPreviewView*>();
        view->resize(960, 540);
        const QImage frame = view->grabFramebuffer();
        printProblems(tab);
        if (argc > 1)
            frame.save(QString::fromLocal8Bit(argv[1]));
    }

    std::printf(g_failures ? "%d failures\n" : "all passed\n", g_failures);
    return g_failures ? 1 : 0;
}
