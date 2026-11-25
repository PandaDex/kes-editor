#include "modules/map/MapEditorTab.h"
#include "compile/MapCompiler.h"
#include "core/EditorContext.h"
#include "core/MapOperations.h"
#include "render/Viewport.h"
#include "ui/EntityClassDialog.h"
#include "ui/KesStyle.h"
#include "ui/OutlinerPanel.h"
#include "ui/PropertiesPanel.h"
#include "ui/Theme.h"
#include <QActionGroup>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QSplitter>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTimer>
#include <QToolBar>
#include <QUndoView>

EditorTab* MapModule::createTab(EditorContext* ctx) const
{
    return new MapEditorTab(ctx);
}

MapEditorTab::MapEditorTab(EditorContext* ctx, QWidget* parent)
    : EditorTab(ctx, QStringLiteral("map"), parent)
    , m_ctx(ctx)
{
    setObjectName("MapEditor");
    setCorner(Qt::BottomLeftCorner, Qt::LeftDockWidgetArea);
    setCorner(Qt::BottomRightCorner, Qt::RightDockWidgetArea);

    createActions();
    createViewports();
    createDocks();
    createMenus();
    createToolbars();
    createStatusBar();

    connect(m_ctx->doc(), &MapDocument::modifiedChanged, this, &EditorTab::titleChanged);
    connect(m_ctx->doc(), &MapDocument::pathChanged, this, &EditorTab::titleChanged);

    restoreLayout();
    m_ctx->setActiveViewport(m_views[0]);

    auto* autosaveTimer = new QTimer(this);
    autosaveTimer->setInterval(3 * 60 * 1000);
    connect(autosaveTimer, &QTimer::timeout, this, &MapEditorTab::autosave);
    autosaveTimer->start();
}

MapEditorTab::~MapEditorTab()
{
    disconnectChildSignals();
}

void MapEditorTab::disconnectChildSignals()
{
    for (QObject* sender : findChildren<QObject*>())
        QObject::disconnect(sender, nullptr, this, nullptr);
}

QString MapEditorTab::filePath() const
{
    return m_ctx->doc()->filePath();
}

QString MapEditorTab::displayName() const
{
    const QString p = filePath();
    return p.isEmpty() ? tr("untitled map") : QFileInfo(p).fileName();
}

bool MapEditorTab::isModified() const
{
    return m_ctx->doc()->isModified();
}

void MapEditorTab::activated()
{
    if (m_maximizedView) m_maximizedView->setFocus();
}

void MapEditorTab::createActions()
{
    auto* doc = m_ctx->doc();
    auto mk = [this](const QString& text, const QKeySequence& key = {}, const QString& icon = {})
    {
        auto* a = new QAction(text, this);
        if (!icon.isEmpty())
            a->setIcon(kes::style::icon(icon));
        if (!key.isEmpty())
            a->setShortcut(key);
        return a;
    };

    // Undo/redo go through toolBusy() so they can't fire in the middle of a drag.
    QUndoStack* stack = doc->undoStack();
    m_undoAct = mk(tr("&Undo"), QKeySequence::Undo, "undo");
    m_redoAct = mk(tr("&Redo"), {}, "redo");
    m_redoAct->setShortcuts({ QKeySequence::Redo, QKeySequence("Ctrl+Y") });
    m_undoAct->setEnabled(stack->canUndo());
    m_redoAct->setEnabled(stack->canRedo());
    connect(stack, &QUndoStack::canUndoChanged, m_undoAct, &QAction::setEnabled);
    connect(stack, &QUndoStack::canRedoChanged, m_redoAct, &QAction::setEnabled);
    connect(stack, &QUndoStack::undoTextChanged, this, [this](const QString& t)
        { m_undoAct->setText(t.isEmpty() ? tr("&Undo") : tr("&Undo %1").arg(t)); });
    connect(stack, &QUndoStack::redoTextChanged, this, [this](const QString& t)
        { m_redoAct->setText(t.isEmpty() ? tr("&Redo") : tr("&Redo %1").arg(t)); });
    connect(m_undoAct, &QAction::triggered, this, [this, stack]
        { if (!toolBusy()) stack->undo(); });
    connect(m_redoAct, &QAction::triggered, this, [this, stack]
        { if (!toolBusy()) stack->redo(); });

    m_cutAct = mk(tr("Cu&t"), QKeySequence::Cut);
    m_copyAct = mk(tr("&Copy"), QKeySequence::Copy);
    m_pasteAct = mk(tr("&Paste"), QKeySequence::Paste);
    m_selClassAct = mk(tr("Select All of Same &Class"), QKeySequence("Ctrl+Shift+E"));
    m_hideSelAct = mk(tr("&Hide Selected"), QKeySequence(Qt::Key_H));
    m_hideUnselAct = mk(tr("Hide &Unselected"), QKeySequence("Shift+H"));
    m_unhideAct = mk(tr("Unhide &All"), QKeySequence(Qt::Key_U));
    connect(m_cutAct, &QAction::triggered, this, [this]
        { if (!toolBusy()) MapOps::cut(m_ctx); });
    connect(m_copyAct, &QAction::triggered, this, [this]
        { MapOps::copy(m_ctx); });
    connect(m_pasteAct, &QAction::triggered, this, [this]
        {
        if (toolBusy()) return;
        if (!MapOps::paste(m_ctx)) statusBar()->showMessage(tr("Nothing to paste."), 3000); });
    connect(m_selClassAct, &QAction::triggered, this, [this]
        { MapOps::selectSameClass(m_ctx); });
    connect(m_hideSelAct, &QAction::triggered, this, [this]
        { if (!toolBusy()) MapOps::hideSelected(m_ctx); });
    connect(m_hideUnselAct, &QAction::triggered, this, [this]
        { if (!toolBusy()) MapOps::hideUnselected(m_ctx); });
    connect(m_unhideAct, &QAction::triggered, this, [this]
        { if (!toolBusy()) MapOps::unhideAll(m_ctx); });

    m_carveAct = mk(tr("&Carve"), QKeySequence("Ctrl+Shift+C"));
    m_hollowAct = mk(tr("&Hollow..."), QKeySequence("Ctrl+H"));
    m_tieAct = mk(tr("&Tie to Entity..."), QKeySequence("Ctrl+T"));
    m_toWorldAct = mk(tr("&Move to World"), QKeySequence("Ctrl+Shift+W"));
    m_transformAct = mk(tr("T&ransform..."), QKeySequence("Ctrl+M"));
    m_snapSelAct = mk(tr("&Snap Selected to Grid"), QKeySequence("Ctrl+B"));
    m_flipHAct = mk(tr("Flip &Horizontally"), QKeySequence("Ctrl+L"));
    m_flipVAct = mk(tr("Flip &Vertically"), QKeySequence("Ctrl+I"));
    m_checkAct = mk(tr("&Check for Problems"), QKeySequence("Alt+P"));
    m_carveAct->setToolTip(tr("Subtract the selected solids from every solid they touch (Ctrl+Shift+C)"));
    m_hollowAct->setToolTip(tr("Turn the selected solids into hollow rooms (Ctrl+H)"));
    m_tieAct->setToolTip(tr("Combine the selected solids into one solid entity (Ctrl+T)"));
    m_toWorldAct->setToolTip(tr("Turn the selected solid entities back into static geometry"));
    m_checkAct->setToolTip(tr("Validate the map without building it (Alt+P)"));
    connect(m_carveAct, &QAction::triggered, this, [this]
        { if (!toolBusy()) MapOps::carve(m_ctx); });
    connect(m_hollowAct, &QAction::triggered, this, &MapEditorTab::hollow);
    connect(m_tieAct, &QAction::triggered, this, &MapEditorTab::tieToEntity);
    connect(m_toWorldAct, &QAction::triggered, this, [this]
        { if (!toolBusy()) MapOps::moveToWorld(m_ctx); });
    connect(m_transformAct, &QAction::triggered, this, &MapEditorTab::showTransformDialog);
    connect(m_snapSelAct, &QAction::triggered, this, [this]
        { if (!toolBusy()) MapOps::snapToGrid(m_ctx); });
    connect(m_flipHAct, &QAction::triggered, this, [this]
        { flipSelection(false); });
    connect(m_flipVAct, &QAction::triggered, this, [this]
        { flipSelection(true); });
    connect(m_checkAct, &QAction::triggered, this, &MapEditorTab::checkProblems);

    m_dupAct = mk(tr("&Duplicate"), QKeySequence("Ctrl+D"));
    m_delAct = mk(tr("D&elete"), QKeySequence::Delete);
    m_selAllAct = mk(tr("Select &All"), QKeySequence::SelectAll);
    m_selNoneAct = mk(tr("Select &None"), QKeySequence("Ctrl+Shift+A"));
    m_centerAct = mk(tr("&Center Views on Selection"), QKeySequence("Ctrl+E"));
    connect(m_dupAct, &QAction::triggered, this, [this]
        {
        if (toolBusy()) return;
        const float g = float(m_ctx->gridSize());
        m_ctx->doc()->duplicateSelection({g, g, 0}); });
    connect(m_delAct, &QAction::triggered, this, [this]
        { if (!toolBusy()) m_ctx->doc()->deleteObjects(m_ctx->doc()->selection()); });
    connect(m_selAllAct, &QAction::triggered, this, [this]
        { m_ctx->doc()->selectAll(); });
    connect(m_selNoneAct, &QAction::triggered, this, [this]
        { m_ctx->doc()->clearSelection(); });
    connect(m_centerAct, &QAction::triggered, this, [this]
        {
        AABB b = m_ctx->doc()->selectionBounds();
        if (!b.valid()) for (const auto& kvp : m_ctx->doc()->objects()) b.expand(kvp.second.worldBounds());
        m_ctx->focusOn(b); });

    m_snapAct = mk(tr("Snap to &Grid"), QKeySequence("Shift+W"));
    m_snapAct->setCheckable(true);
    m_snapAct->setChecked(m_ctx->snapEnabled());
    m_gridUpAct = mk(tr("&Larger Grid"), QKeySequence(Qt::Key_BracketRight));
    m_gridDownAct = mk(tr("&Smaller Grid"), QKeySequence(Qt::Key_BracketLeft));
    m_maxAct = mk(tr("&Maximize Active View"), QKeySequence("Shift+Z"));
    m_perspectiveAct = mk(tr("3D Only"));
    m_perspectiveAct->setCheckable(true);
    m_perspectiveAct->setToolTip(tr("Show the 3D perspective view by itself"));
    connect(m_snapAct, &QAction::toggled, m_ctx, &EditorContext::setSnapEnabled);
    connect(m_gridUpAct, &QAction::triggered, this, [this]
        { m_ctx->setGridSize(m_ctx->gridSize() * 2); });
    connect(m_gridDownAct, &QAction::triggered, this, [this]
        { m_ctx->setGridSize(m_ctx->gridSize() / 2); });
    connect(m_maxAct, &QAction::triggered, this, &MapEditorTab::toggleMaximize);
    connect(m_perspectiveAct, &QAction::toggled, this, &MapEditorTab::togglePerspectiveView);

    m_shadingGroup = new QActionGroup(this);
    const QString shadingNames[3] = { tr("&Shaded"), tr("&Unlit"), tr("&Lit") };
    const QString shadingTips[3] = { tr("Editing view: textures with a fixed key light"),
        tr("Base colour and emissive only, no lighting"),
        tr("The game's lights, sky, shadows and tonemap") };
    for (int i = 0; i < 3; ++i)
    {
        QAction* a = mk(shadingNames[i], QKeySequence(QString("Alt+%1").arg(i + 1)));
        a->setCheckable(true);
        a->setToolTip(shadingTips[i]);
        a->setData(i);
        a->setChecked(int(m_ctx->shadingMode()) == i);
        m_shadingGroup->addAction(a);
        connect(a, &QAction::triggered, this, [this, i]
            { m_ctx->setShadingMode(ShadingMode(i)); });
    }

    m_buildAct = mk(tr("&Build Map..."), QKeySequence(Qt::Key_F9));
    m_newClassAct = mk(tr("&New Entity Class..."));
    m_reloadDefsAct = mk(tr("&Reload Entity Definitions"), QKeySequence("Ctrl+Shift+R"));
    connect(m_buildAct, &QAction::triggered, this, &MapEditorTab::buildMap);
    connect(m_newClassAct, &QAction::triggered, this, [this]
        { EntityClassDialog(m_ctx, this).exec(); });
    connect(m_reloadDefsAct, &QAction::triggered, this, [this]
        { m_ctx->registry()->reloadAll(); });

    m_toolGroup = new QActionGroup(this);
    for (Tool* t : m_ctx->tools()->tools())
    {
        auto* a = new QAction(Theme::toolIcon(t->id()), t->title(), this);
        a->setCheckable(true);
        if (!t->shortcut().isEmpty())
            a->setShortcut(QKeySequence(t->shortcut()));
        a->setToolTip(QString("<b>%1</b> (%2)<br>%3").arg(t->title(), t->shortcut(), t->statusHint().toHtmlEscaped()));
        m_toolGroup->addAction(a);
        m_toolActions[t->id()] = a;
        const QString id = t->id();
        connect(a, &QAction::triggered, this, [this, id]
            {
            if (toolBusy()) {
                if (auto *cur = m_toolActions.value(m_ctx->tools()->current()->id())) cur->setChecked(true);
                return;
            }
            m_ctx->tools()->setCurrent(id); });
    }
    connect(m_ctx->tools(), &ToolManager::currentChanged, this, [this](Tool* t)
        {
        if (auto* a = m_toolActions.value(t->id())) a->setChecked(true);
        if (m_optionsStack && m_optionsIndex.contains(t->id())) m_optionsStack->setCurrentIndex(m_optionsIndex[t->id()]);
        statusBar()->showMessage(t->statusHint());
        for (Viewport* v : m_ctx->viewports()) v->update(); });
    if (auto* a = m_toolActions.value(m_ctx->tools()->current()->id()))
        a->setChecked(true);
}

void MapEditorTab::createMenus()
{
    QMenu* edit = addTabMenu(tr("&Edit"));
    edit->addActions({ m_undoAct, m_redoAct });
    edit->addSeparator();
    edit->addActions({ m_cutAct, m_copyAct, m_pasteAct, m_dupAct, m_delAct });
    edit->addSeparator();
    edit->addActions({ m_selAllAct, m_selNoneAct, m_selClassAct, m_centerAct });
    edit->addSeparator();
    edit->addActions({ m_hideSelAct, m_hideUnselAct, m_unhideAct });
    connect(edit, &QMenu::aboutToShow, this, [this]
        { m_pasteAct->setEnabled(MapOps::canPaste()); });
    connect(edit, &QMenu::aboutToHide, this, [this]
        { m_pasteAct->setEnabled(true); });

    QMenu* view = addTabMenu(tr("&View"));
    view->addActions({ m_snapAct, m_gridUpAct, m_gridDownAct });
    view->addSeparator();
    view->addActions({ m_maxAct, m_perspectiveAct });
    view->addSeparator();
    view->addMenu(tr("3D S&hading"))->addActions(m_shadingGroup->actions());
    view->addSeparator();
    QMenu* panels = view->addMenu(tr("&Panels"));
    for (QDockWidget* d : findChildren<QDockWidget*>())
        panels->addAction(d->toggleViewAction());

    QMenu* tools = addTabMenu(tr("&Tools"));
    for (Tool* t : m_ctx->tools()->tools())
        tools->addAction(m_toolActions[t->id()]);
    tools->addSeparator();
    tools->addActions({ m_carveAct, m_hollowAct });
    tools->addSeparator();
    tools->addActions({ m_tieAct, m_toWorldAct });
    tools->addSeparator();
    tools->addActions({ m_transformAct, m_snapSelAct, m_flipHAct, m_flipVAct });
    tools->addSeparator();
    tools->addActions({ m_newClassAct, m_reloadDefsAct });

    QMenu* map = addTabMenu(tr("&Map"));
    map->addAction(m_checkAct);
    map->addAction(m_buildAct);
}

void MapEditorTab::createToolbars()
{
    auto* strip = new QToolBar(tr("Tools"), this);
    strip->setObjectName("toolStrip");
    strip->setMovable(false);
    strip->setIconSize(QSize(26, 26));
    strip->setToolButtonStyle(Qt::ToolButtonIconOnly);
    for (Tool* t : m_ctx->tools()->tools())
        strip->addAction(m_toolActions[t->id()]);
    addToolBar(Qt::LeftToolBarArea, strip);

    auto* top = new QToolBar(tr("Main"), this);
    top->setObjectName("mainBar");
    top->setMovable(false);
    top->setIconSize(QSize(18, 18));
    top->addActions({ m_undoAct, m_redoAct });
    top->addSeparator();
    top->addWidget(new QLabel(tr(" Grid ")));
    m_gridBox = new QComboBox;
    for (int g = 1; g <= 512; g *= 2)
        m_gridBox->addItem(QString::number(g), g);
    m_gridBox->setCurrentIndex(m_gridBox->findData(m_ctx->gridSize()));
    top->addWidget(m_gridBox);
    top->addAction(m_snapAct);
    top->addSeparator();
    top->addAction(m_perspectiveAct);
    top->addWidget(new QLabel(tr(" Shading ")));
    m_shadingBox = new QComboBox;
    m_shadingBox->addItems({ tr("Shaded"), tr("Unlit"), tr("Lit") });
    m_shadingBox->setToolTip(tr("How the 3D view draws materials (Alt+1/2/3)"));
    m_shadingBox->setCurrentIndex(int(m_ctx->shadingMode()));
    top->addWidget(m_shadingBox);
    top->addSeparator();
    top->addActions({ m_carveAct, m_hollowAct, m_tieAct });
    top->addSeparator();
    top->addActions({ m_checkAct, m_buildAct });
    addToolBar(Qt::TopToolBarArea, top);

    connect(m_shadingBox, qOverload<int>(&QComboBox::activated), this, [this](int i)
        { m_ctx->setShadingMode(ShadingMode(i)); });
    connect(m_ctx, &EditorContext::shadingModeChanged, this, [this]
        {
        const int mode = int(m_ctx->shadingMode());
        m_shadingBox->setCurrentIndex(mode);
        m_shadingGroup->actions()[mode]->setChecked(true); });
    connect(m_gridBox, qOverload<int>(&QComboBox::activated), this, [this](int i)
        { m_ctx->setGridSize(m_gridBox->itemData(i).toInt()); });
    connect(m_ctx, &EditorContext::gridChanged, this, [this]
        {
        const int i = m_gridBox->findData(m_ctx->gridSize());
        QSignalBlocker b(m_gridBox);
        if (i >= 0) m_gridBox->setCurrentIndex(i);
        m_stGrid->setText(tr("Grid: %1 %2").arg(m_ctx->gridSize()).arg(m_ctx->snapEnabled() ? tr("(snap)") : tr("(free)"))); });
    connect(m_ctx, &EditorContext::snapChanged, this, [this]
        {
        QSignalBlocker b(m_snapAct);
        m_snapAct->setChecked(m_ctx->snapEnabled());
        m_stGrid->setText(tr("Grid: %1 %2").arg(m_ctx->gridSize()).arg(m_ctx->snapEnabled() ? tr("(snap)") : tr("(free)"))); });
}

void MapEditorTab::createViewports()
{
    const ViewKind kinds[4] = { ViewKind::Perspective, ViewKind::Top, ViewKind::Front, ViewKind::Side };
    for (int i = 0; i < 4; ++i)
        m_views[i] = new Viewport(m_ctx, kinds[i]);
    m_rows[0] = new QSplitter(Qt::Horizontal);
    m_rows[1] = new QSplitter(Qt::Horizontal);
    m_rows[0]->addWidget(m_views[0]);
    m_rows[0]->addWidget(m_views[1]);
    m_rows[1]->addWidget(m_views[2]);
    m_rows[1]->addWidget(m_views[3]);
    m_vsplit = new QSplitter(Qt::Vertical);
    m_vsplit->addWidget(m_rows[0]);
    m_vsplit->addWidget(m_rows[1]);
    for (QSplitter* s : { m_vsplit, m_rows[0], m_rows[1] })
    {
        s->setChildrenCollapsible(false);
        s->setHandleWidth(3);
    }
    connect(m_rows[0], &QSplitter::splitterMoved, this, [this]
        { m_rows[1]->setSizes(m_rows[0]->sizes()); });
    connect(m_rows[1], &QSplitter::splitterMoved, this, [this]
        { m_rows[0]->setSizes(m_rows[1]->sizes()); });
    setCentralWidget(m_vsplit);
}

void MapEditorTab::createDocks()
{
    auto addDock = [this](const QString& title, QWidget* w, Qt::DockWidgetArea area)
    {
        auto* d = new QDockWidget(title, this);
        d->setObjectName(title);
        d->setWidget(w);
        addDockWidget(area, d);
        return d;
    };

    m_optionsStack = new QStackedWidget;
    for (Tool* t : m_ctx->tools()->tools())
    {
        QWidget* panel = t->createOptionsPanel(m_optionsStack);
        if (!panel)
            panel = new QWidget;
        m_optionsIndex[t->id()] = m_optionsStack->addWidget(panel);
    }
    m_optionsStack->setCurrentIndex(m_optionsIndex.value(m_ctx->tools()->current()->id()));

    auto* outliner = addDock(tr("Outliner"), new OutlinerPanel(m_ctx), Qt::RightDockWidgetArea);
    auto* options = addDock(tr("Tool Options"), m_optionsStack, Qt::RightDockWidgetArea);
    auto* props = addDock(tr("Properties"), new PropertiesPanel(m_ctx), Qt::RightDockWidgetArea);
    splitDockWidget(outliner, props, Qt::Vertical);
    tabifyDockWidget(outliner, options);
    outliner->raise();

    auto* historyView = new QUndoView(m_ctx->doc()->undoStack());
    historyView->setEmptyLabel(tr("<original map>"));
    auto* history = addDock(tr("History"), historyView, Qt::RightDockWidgetArea);
    tabifyDockWidget(props, history);
    props->raise();

    resizeDocks({ outliner, props }, { 380, 460 }, Qt::Vertical);
    resizeDocks({ outliner }, { 360 }, Qt::Horizontal);
}

void MapEditorTab::createStatusBar()
{
    m_stCursor = new QLabel;
    m_stSel = new QLabel;
    m_stGrid = new QLabel;
    for (QLabel* l : { m_stCursor, m_stSel, m_stGrid })
    {
        l->setMinimumWidth(130);
        statusBar()->addPermanentWidget(l);
    }
    m_stGrid->setText(tr("Grid: %1 %2").arg(m_ctx->gridSize()).arg(tr("(snap)")));
    connect(m_ctx, &EditorContext::cursorText, m_stCursor, &QLabel::setText);
    connect(m_ctx->doc(), &MapDocument::selectionChanged, this, &MapEditorTab::updateSelectionStatus);
    connect(m_ctx->doc(), &MapDocument::objectsChanged, this, &MapEditorTab::updateSelectionStatus);
    updateSelectionStatus();
    statusBar()->showMessage(m_ctx->tools()->current()->statusHint());
}

void MapEditorTab::updateSelectionStatus()
{
    const auto& sel = m_ctx->doc()->selection();
    if (sel.isEmpty())
    {
        m_stSel->setText(tr("%1 objects").arg(m_ctx->doc()->objects().size()));
        return;
    }
    const QVector3D s = m_ctx->doc()->selectionBounds().size();
    m_stSel->setText(tr("Selected %1  (%2 x %3 x %4)").arg(sel.size()).arg(int(s.x())).arg(int(s.y())).arg(int(s.z())));
}

void MapEditorTab::toggleMaximize()
{
    Viewport* active = m_ctx->activeViewport();
    if (!active)
        return;

    setMaximizedView(m_maximizedView == active ? nullptr : active);
}

void MapEditorTab::togglePerspectiveView(bool maximized)
{
    if (maximized)
    {
        setMaximizedView(m_views[0]);
    }
    else if (m_maximizedView == m_views[0])
    {
        setMaximizedView(nullptr);
    }
}

void MapEditorTab::setMaximizedView(Viewport* view)
{
    m_maximizedView = view;
    bool vis[4];
    for (int i = 0; i < 4; ++i)
        vis[i] = !m_maximizedView || m_views[i] == m_maximizedView;
    for (int i = 0; i < 4; ++i)
        m_views[i]->setVisible(vis[i]);
    m_rows[0]->setVisible(vis[0] || vis[1]);
    m_rows[1]->setVisible(vis[2] || vis[3]);
    if (m_maximizedView)
        m_maximizedView->setFocus();

    const QSignalBlocker blocker(m_perspectiveAct);
    m_perspectiveAct->setChecked(m_maximizedView == m_views[0]);
}

void MapEditorTab::buildMap()
{
    const QString mapPath = m_ctx->doc()->filePath();
    const QFileInfo sourceInfo(mapPath);
    const QString defaultName = mapPath.isEmpty()
        ? QDir(m_ctx->mapsDir()).filePath("untitled.fmapc")
        : sourceInfo.dir().filePath(sourceInfo.completeBaseName() + ".fmapc");
    QString outputPath = QFileDialog::getSaveFileName(
        this, tr("Build Map"), defaultName, tr("Forge Compiled Map (*.fmapc)"));
    if (outputPath.isEmpty())
        return;
    if (QFileInfo(outputPath).suffix().isEmpty())
        outputPath += ".fmapc";
    if (!mapPath.isEmpty() &&
        QFileInfo(outputPath).absoluteFilePath().compare(QFileInfo(mapPath).absoluteFilePath(), Qt::CaseInsensitive) == 0)
    {
        QMessageBox::warning(this, tr("Build Map"), tr("The compiled output must not overwrite the source map."));
        return;
    }

    CompileContext context;
    context.settings.outputPath = outputPath;
    context.registry = m_ctx->registry();
    context.materials = m_ctx->materials();
    context.map = m_ctx->doc()->snapshot();
    const bool success = createMapCompiler()->compile(context);
    for (const QString& line : context.log)
    {
        const LogLevel level = line.startsWith("Error:") ? LogLevel::Error
            : line.startsWith("Warning:")                ? LogLevel::Warning
                                                         : LogLevel::Info;
        m_ctx->log(line, level);
    }

    if (success)
    {
        statusBar()->showMessage(tr("Map compiled: %1").arg(QFileInfo(outputPath).fileName()), 6000);
        return;
    }

    const QString details = context.log.join('\n');
    QMessageBox::critical(this, tr("Build Map Failed"),
        tr("The map could not be compiled.\n\n%1").arg(details));
}

bool MapEditorTab::save()
{
    if (m_ctx->doc()->filePath().isEmpty())
        return saveAs();
    QString err;
    if (!m_ctx->doc()->save(m_ctx->doc()->filePath(), &err))
    {
        QMessageBox::critical(this, tr("Save failed"), err);
        return false;
    }
    m_ctx->log(tr("Saved %1").arg(m_ctx->doc()->filePath()));
    emit fileSaved(m_ctx->doc()->filePath());
    return true;
}

bool MapEditorTab::saveAs()
{
    QString f = QFileDialog::getSaveFileName(this, tr("Save Map"), m_ctx->mapsDir(), tr("Forge Map (*.fmap)"));
    if (f.isEmpty())
        return false;
    if (!f.endsWith(".fmap"))
        f += ".fmap";
    QString err;
    if (!m_ctx->doc()->save(f, &err))
    {
        QMessageBox::critical(this, tr("Save failed"), err);
        return false;
    }
    m_ctx->log(tr("Saved %1").arg(f));
    emit fileSaved(f);
    return true;
}

void MapEditorTab::newDocument()
{
    if (!toolBusy())
        m_ctx->doc()->clear();
}

bool MapEditorTab::openFile(const QString& path, QString* error)
{
    if (toolBusy())
    {
        if (error)
            *error = tr("Finish or cancel the current drag first.");
        return false;
    }
    QString err;
    if (!m_ctx->doc()->load(path, &err))
    {
        if (error)
            *error = err;
        return false;
    }
    m_ctx->log(tr("Opened %1 (%2 objects)").arg(path).arg(m_ctx->doc()->objects().size()));
    AABB all;
    for (const auto& kvp : m_ctx->doc()->objects())
        all.expand(kvp.second.worldBounds());
    if (all.valid())
        m_ctx->focusOn(all);
    return true;
}

bool MapEditorTab::toolBusy()
{
    if (!m_ctx->tools()->isBusy())
        return false;
    statusBar()->showMessage(tr("Finish or cancel (Esc) the current drag first."), 3000);
    return true;
}

void MapEditorTab::autosave()
{
    MapDocument* doc = m_ctx->doc();
    if (!doc->isModified() || doc->objects().empty() || m_ctx->tools()->isBusy())
        return;
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/autosave";
    QDir().mkpath(dir);
    const QString base = doc->filePath().isEmpty() ? QStringLiteral("untitled") : QFileInfo(doc->filePath()).completeBaseName();
    const QString path = QDir(dir).filePath(base + ".autosave.fmap");
    QString err;
    if (doc->saveCopy(path, &err))
        statusBar()->showMessage(tr("Autosaved to %1").arg(QDir::toNativeSeparators(path)), 4000);
    else
        m_ctx->log(tr("Autosave failed: %1").arg(err), LogLevel::Warning);
}

void MapEditorTab::checkProblems()
{
    CompileContext context;
    context.registry = m_ctx->registry();
    context.map = m_ctx->doc()->snapshot();
    const bool ok = validateMap(context);
    int warnings = 0, errors = 0;
    for (const QString& line : context.log)
    {
        const LogLevel level = line.startsWith("Error:") ? LogLevel::Error
            : line.startsWith("Warning:")                ? LogLevel::Warning
                                                         : LogLevel::Info;
        warnings += level == LogLevel::Warning;
        errors += level == LogLevel::Error;
        m_ctx->log(line, level);
    }
    if (ok && warnings == 0)
    {
        statusBar()->showMessage(tr("Check for Problems: no problems found."), 5000);
        return;
    }
    QMessageBox::warning(this, tr("Check for Problems"),
        tr("%1 error(s), %2 warning(s). See the Console for details.").arg(errors).arg(warnings));
}

void MapEditorTab::tieToEntity()
{
    if (toolBusy())
        return;
    QStringList classes;
    for (const ClassDef* c : m_ctx->registry()->classes(ClassKind::Solid))
        classes << c->name;
    if (classes.isEmpty())
    {
        QMessageBox::information(this, tr("Tie to Entity"), tr("No solid entity classes are defined. Add a @SolidClass to an .dec file."));
        return;
    }
    static QString lastClass = QStringLiteral("func_detail");
    const int current = std::max<int>(0, int(classes.indexOf(lastClass)));
    bool ok = false;
    const QString cls = QInputDialog::getItem(this, tr("Tie to Entity"), tr("Solid entity class:"), classes, current, false, &ok);
    if (!ok || cls.isEmpty())
        return;
    lastClass = cls;
    MapOps::tieToEntity(m_ctx, cls);
}

void MapEditorTab::hollow()
{
    if (toolBusy())
        return;
    static int thickness = 16;
    bool ok = false;
    const int t = QInputDialog::getInt(this, tr("Hollow"), tr("Wall thickness (negative grows outward):"), thickness, -1024, 1024, 1, &ok);
    if (!ok || t == 0)
        return;
    thickness = t;
    MapOps::hollow(m_ctx, float(t));
}

void MapEditorTab::flipSelection(bool vertical)
{
    if (toolBusy())
        return;
    const Viewport* v = m_ctx->activeViewport();
    int axis = vertical ? 2 : 0;
    if (v && v->isOrtho())
        axis = vertical ? v->camera().axisV() : v->camera().axisH();
    MapOps::flip(m_ctx, axis);
}

void MapEditorTab::showTransformDialog()
{
    if (toolBusy() || m_ctx->doc()->selection().isEmpty())
        return;
    QDialog dlg(this);
    dlg.setWindowTitle(tr("Transform"));
    auto* form = new QFormLayout(&dlg);
    auto* mode = new QComboBox(&dlg);
    mode->addItems({ tr("Move"), tr("Rotate (degrees)"), tr("Scale") });
    form->addRow(tr("Mode"), mode);
    QDoubleSpinBox* val[3];
    auto* row = new QWidget(&dlg);
    auto* h = new QHBoxLayout(row);
    h->setContentsMargins(0, 0, 0, 0);
    const char* axes = "XYZ";
    for (int i = 0; i < 3; ++i)
    {
        val[i] = new QDoubleSpinBox(row);
        val[i]->setRange(-65536, 65536);
        val[i]->setDecimals(3);
        val[i]->setPrefix(QString(QChar(axes[i])) + ": ");
        h->addWidget(val[i]);
    }
    form->addRow(tr("Values"), row);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    connect(mode, qOverload<int>(&QComboBox::currentIndexChanged), &dlg, [val](int m)
        { for (auto *s : val) s->setValue(m == 2 ? 1.0 : 0.0); });
    if (dlg.exec() != QDialog::Accepted)
        return;

    const QVector3D v(float(val[0]->value()), float(val[1]->value()), float(val[2]->value()));
    const QVector3D c = m_ctx->doc()->selectionBounds().center();
    QMatrix4x4 m;
    switch (mode->currentIndex())
    {
    case 0:
        m.translate(v);
        MapOps::transformSelection(m_ctx, m, tr("Transform: Move"));
        break;
    case 1:
        m.translate(c);
        m.rotate(v.z(), 0, 0, 1);
        m.rotate(v.y(), 0, 1, 0);
        m.rotate(v.x(), 1, 0, 0);
        m.translate(-c);
        MapOps::transformSelection(m_ctx, m, tr("Transform: Rotate"), true);
        break;
    default:
        if (std::abs(v.x()) < 1e-3f || std::abs(v.y()) < 1e-3f || std::abs(v.z()) < 1e-3f)
        {
            QMessageBox::warning(this, tr("Transform"), tr("Scale factors can't be zero."));
            return;
        }
        m.translate(c);
        m.scale(v);
        m.translate(-c);
        MapOps::transformSelection(m_ctx, m, tr("Transform: Scale"));
        break;
    }
}
