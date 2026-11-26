#pragma once
#include "framework/EditorModule.h"
#include "framework/EditorTab.h"
#include <QMap>

class Viewport;
class QActionGroup;
class QComboBox;
class QLabel;
class QSplitter;
class QStackedWidget;

// The level editor: four viewports, map tools, outliner and properties. There is one map tab;
// it edits EditorContext::doc().
class MapEditorTab : public EditorTab
{
    Q_OBJECT
public:
    explicit MapEditorTab(EditorContext* ctx, QWidget* parent = nullptr);
    ~MapEditorTab() override;

    QString filePath() const override;
    QString displayName() const override;
    bool isModified() const override;
    bool openFile(const QString& path, QString* error) override;
    void newDocument() override;
    bool save() override;
    bool saveAs() override;
    void activated() override;

    // false (with a status message) while a viewport drag is in progress
    bool toolBusy();

private:
    void disconnectChildSignals();
    void createActions();
    void createMenus();
    void createToolbars();
    void createViewports();
    void createDocks();
    void createStatusBar();

    void updateSelectionStatus();
    void toggleMaximize();
    void togglePerspectiveView(bool maximized);
    void setMaximizedView(Viewport* view);
    void buildMap();
    void autosave();
    void checkProblems();
    void showTransformDialog();
    void tieToEntity();
    void hollow();
    void flipSelection(bool vertical);

    Viewport* m_views[4] = {};
    QSplitter* m_vsplit = nullptr;
    QSplitter* m_rows[2] = {};
    Viewport* m_maximizedView = nullptr;

    QActionGroup* m_toolGroup = nullptr;
    QMap<QString, QAction*> m_toolActions;
    QAction *m_undoAct, *m_redoAct, *m_dupAct, *m_delAct, *m_selAllAct, *m_selNoneAct, *m_centerAct;
    QAction *m_snapAct, *m_gridUpAct, *m_gridDownAct, *m_maxAct, *m_perspectiveAct;
    QAction *m_buildAct, *m_newClassAct, *m_reloadDefsAct;
    QAction *m_cutAct, *m_copyAct, *m_pasteAct, *m_selClassAct;
    QAction *m_hideSelAct, *m_hideUnselAct, *m_unhideAct;
    QAction *m_carveAct, *m_hollowAct, *m_tieAct, *m_toWorldAct, *m_transformAct, *m_snapSelAct;
    QAction *m_flipHAct, *m_flipVAct, *m_checkAct;
    QComboBox* m_gridBox = nullptr;
    QComboBox* m_shadingBox = nullptr;
    QActionGroup* m_shadingGroup = nullptr;
    QStackedWidget* m_optionsStack = nullptr;
    QMap<QString, int> m_optionsIndex;
    QLabel *m_stCursor = nullptr, *m_stSel = nullptr, *m_stGrid = nullptr;
    EditorContext* m_ctx;
};

class MapModule : public EditorModule
{
public:
    QString id() const override { return QStringLiteral("map"); }
    QString name() const override { return QStringLiteral("KES Map"); }
    QStringList extensions() const override { return { QStringLiteral("fmap") }; }
    QString newLabel() const override { return QStringLiteral("Map"); }
    bool allowsMultipleTabs() const override { return false; }
    bool closable() const override { return false; }
    EditorTab* createTab(EditorContext* ctx) const override;
};
