#pragma once
#include "framework/EditorModule.h"
#include <QMainWindow>

class AssetBrowser;
class AssetImporter;
class EditorContext;
class EditorTab;
class MapEditorTab;
class QDockWidget;
class QMenu;
class QTabWidget;

// Forge's main window: a tab per open document (the map is always the first tab), the menus of
// the current tab, and the docks every editor shares (Asset Browser, Console).
class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    MainWindow();
    ~MainWindow() override;

    // Opens the file in the editor module that handles its extension.
    bool openFile(const QString& path);

protected:
    void closeEvent(QCloseEvent*) override;
    void dragEnterEvent(QDragEnterEvent*) override;
    void dropEvent(QDropEvent*) override;

private:
    void createFileMenu();
    void createAssetsMenu();
    void createWindowMenu();
    void createDocks();
    void rebuildMenuBar();
    void syncFloatingDocks();

    EditorTab* currentTab() const;
    EditorTab* tabAt(int index) const;
    void addTab(EditorTab* tab, bool closable);
    bool closeTab(int index);
    void newDocument(const EditorModule* module);
    void openProjectTool(const EditorModule* module);
    void open();
    void save();
    void saveAs();
    void saveAll();
    void onTabTitleChanged();
    void onFileSaved(const QString& path);
    void updateTitle();
    void updateTabText(EditorTab* tab);

    bool openProject(const QString& path = QString());
    void promptForProject();
    void chooseContentDir();
    void addRecentFile(const QString& path);
    void rebuildRecentMenu();

    EditorContext* m_ctx;
    ModuleRegistry m_modules;
    AssetImporter* m_importer = nullptr;
    AssetBrowser* m_assets = nullptr;
    QTabWidget* m_tabs = nullptr;
    MapEditorTab* m_mapTab = nullptr;

    QMenu* m_fileMenu = nullptr;
    QMenu* m_recentMenu = nullptr;
    QMenu* m_assetsMenu = nullptr;
    QMenu* m_windowMenu = nullptr;
    QMenu* m_helpMenu = nullptr;
    QAction* m_saveAct = nullptr;
    QAction* m_saveAsAct = nullptr;
    QAction* m_closeTabAct = nullptr;
    QAction* m_projectSettingsAct = nullptr;
    QList<QDockWidget*> m_sharedDocks;
};
