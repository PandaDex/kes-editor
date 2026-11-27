// Editor layout persistence: dock sizes of a tab inside the shell's tab widget survive a restart.
#include "core/EditorContext.h"
#include "framework/EditorTab.h"
#include "ui/OutlinerPanel.h"

#include <QApplication>
#include <QDockWidget>
#include <QHeaderView>
#include <QElapsedTimer>
#include <QLabel>
#include <QMainWindow>
#include <QSettings>
#include <QTabWidget>
#include <QTreeWidget>
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

class TestTab : public EditorTab
{
public:
    TestTab() : EditorTab(nullptr, QStringLiteral("layoutTest"))
    {
        setCentralWidget(new QLabel("view"));
        dock = new QDockWidget("Outliner", this);
        dock->setObjectName("Outliner");
        dock->setWidget(new QLabel("outliner"));
        addDockWidget(Qt::RightDockWidgetArea, dock);
        resizeDocks({ dock }, { 200 }, Qt::Horizontal);
        restoreLayout();
    }
    QString filePath() const override { return {}; }
    bool isModified() const override { return false; }
    bool openFile(const QString&, QString*) override { return true; }
    bool save() override { return true; }
    bool saveAs() override { return true; }

    QDockWidget* dock;
};

struct Shell
{
    QMainWindow window;
    QTabWidget* tabs = new QTabWidget;
    TestTab* tab = new TestTab;
    Shell()
    {
        window.setCentralWidget(tabs);
        tabs->addTab(tab, "map");
        window.resize(1400, 900);
    }
};

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    QApplication::setOrganizationName("kes-tests");
    QApplication::setApplicationName("layout_test");
    QSettings().clear();

    {
        Shell shell;
        shell.window.show();
        wait(app, 200);
        EXPECT(shell.tab->dock->width() < 300);
        shell.tab->resizeDocks({ shell.tab->dock }, { 520 }, Qt::Horizontal);
        wait(app, 100);
        std::printf("first run dock width %d\n", shell.tab->dock->width());
        EXPECT(std::abs(shell.tab->dock->width() - 520) < 12);
        shell.tab->saveLayout();
    }
    {
        Shell shell;
        shell.window.show();
        wait(app, 300);
        std::printf("restored dock width %d\n", shell.tab->dock->width());
        EXPECT(std::abs(shell.tab->dock->width() - 520) < 12);
    }

    // Outliner Hierarchy columns: width and order
    {
        EditorContext ctx;
        {
            OutlinerPanel panel(&ctx);
            panel.resize(400, 500);
            panel.show();
            wait(app, 100);
            QHeaderView* header = panel.findChildren<QTreeWidget*>().first()->header();
            header->resizeSection(0, 230);
            wait(app, 50);
        }
        {
            OutlinerPanel panel(&ctx);
            panel.resize(400, 500);
            panel.show();
            wait(app, 100);
            QHeaderView* header = panel.findChildren<QTreeWidget*>().first()->header();
            std::printf("restored Name column width %d\n", header->sectionSize(0));
            EXPECT(header->sectionSize(0) == 230);
            header->moveSection(1, 0);
            wait(app, 50);
        }
        {
            OutlinerPanel panel(&ctx);
            panel.resize(400, 500);
            panel.show();
            wait(app, 100);
            QHeaderView* header = panel.findChildren<QTreeWidget*>().first()->header();
            std::printf("restored Name column visual index %d\n", header->visualIndex(0));
            EXPECT(header->visualIndex(0) == 1);
        }
    }

    QSettings().clear();
    std::printf(g_failures ? "%d failure(s)\n" : "layout_test passed\n", g_failures);
    return g_failures ? 1 : 0;
}
