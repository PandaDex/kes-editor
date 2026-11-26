#include "ui/MainWindow.h"
#include "assets/AssetEditors.h"
#include "assets/AssetImporter.h"
#include "assets/AssetTemplates.h"
#include "core/EditorContext.h"
#include "framework/EditorTab.h"
#include "modules/map/MapEditorTab.h"
#include "ui/AssetBrowser.h"
#include "ui/BrandDialogs.h"
#include "ui/KesStyle.h"
#include "ui/ProblemsPanel.h"
#include "ui/ProjectSettingsDialog.h"
#include <QApplication>
#include <QCloseEvent>
#include <QDesktopServices>
#include <QDir>
#include <QDockWidget>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QPushButton>
#include <QSettings>
#include <QStyle>
#include <QTabBar>
#include <QTabWidget>
#include <QTimer>
#include <QUrl>

MainWindow::MainWindow()
{
    m_ctx = new EditorContext(this);
    registerBuiltinModules(m_modules);
    AssetEditors::setForgeOpens([this](const QString& path)
        { return m_modules.moduleFor(path) != nullptr; });

    m_importer = new AssetImporter(m_ctx, this);
    connect(m_importer, &AssetImporter::imported, m_ctx->models(), &ModelCache::refresh);
    setObjectName("MainWindow");
    resize(1500, 920);
    setDockNestingEnabled(true);

    m_tabs = new QTabWidget;
    m_tabs->setDocumentMode(true);
    m_tabs->setMovable(true);
    m_tabs->setTabsClosable(true);
    setCentralWidget(m_tabs);
    connect(m_tabs, &QTabWidget::tabCloseRequested, this, [this](int i)
        { closeTab(i); });
    connect(m_tabs, &QTabWidget::currentChanged, this, [this]
        {
        rebuildMenuBar();
        updateTitle();
        syncFloatingDocks();
        if (m_problems) m_problems->scheduleRefresh();
        if (EditorTab* tab = currentTab()) tab->activated(); });

    createDocks();
    createFileMenu();
    createAssetsMenu();
    createWindowMenu();
    m_helpMenu = new QMenu(tr("&Help"), this);
    m_helpMenu->addAction(tr("&About KES Editor"), this, [this]
        { showAbout(this); });

    m_mapTab = static_cast<MapEditorTab*>(m_modules.module("map")->createTab(m_ctx));
    addTab(m_mapTab, false);
    rebuildMenuBar();

    connect(m_ctx, &EditorContext::projectChanged, this, [this]
        {
        m_projectSettingsAct->setEnabled(m_ctx->project().isValid());
        for (int i = 0; i < m_tabs->count(); ++i) tabAt(i)->projectChanged();
        updateTitle(); });
    updateTitle();

    QSettings s;
    if (s.contains("shell/geometry"))
    {
        restoreGeometry(s.value("shell/geometry").toByteArray());
        restoreState(s.value("shell/state").toByteArray());
    }
    setAcceptDrops(true);

    m_ctx->log(tr("KES Editor ready. Entity classes: %1 point, %2 solid.")
            .arg(m_ctx->registry()->classes(ClassKind::Point).size())
            .arg(m_ctx->registry()->classes(ClassKind::Solid).size()));

    if (m_ctx->project().isValid())
        m_ctx->log(tr("Project: %1 (%2)").arg(m_ctx->project().name, m_ctx->project().filePath));
    else
        QTimer::singleShot(0, this, &MainWindow::promptForProject); // once the window is visible
}

MainWindow::~MainWindow()
{
    for (QObject* sender : findChildren<QObject*>())
        QObject::disconnect(sender, nullptr, this, nullptr);
    // tabs go first: they still use the context while they shut down
    while (m_tabs->count() > 0)
    {
        QWidget* w = m_tabs->widget(0);
        m_tabs->removeTab(0);
        delete w;
    }
}

EditorTab* MainWindow::currentTab() const
{
    return qobject_cast<EditorTab*>(m_tabs->currentWidget());
}

EditorTab* MainWindow::tabAt(int index) const
{
    return qobject_cast<EditorTab*>(m_tabs->widget(index));
}

void MainWindow::createFileMenu()
{
    m_fileMenu = new QMenu(tr("&File"), this);

    QMenu* newMenu = m_fileMenu->addMenu(kes::style::icon("new"), tr("&New"));
    for (const EditorModule* module : m_modules.modules())
    {
        if (module->newLabel().isEmpty())
            continue;
        QAction* a = newMenu->addAction(module->newLabel(), this, [this, module]
            { newDocument(module); });
        if (module->id() == "map")
            a->setShortcut(QKeySequence::New);
    }

    QAction* openAct = m_fileMenu->addAction(kes::style::icon("open"), tr("&Open..."), this, &MainWindow::open);
    openAct->setShortcut(QKeySequence::Open);
    m_recentMenu = m_fileMenu->addMenu(tr("Open &Recent"));
    connect(m_recentMenu, &QMenu::aboutToShow, this, &MainWindow::rebuildRecentMenu);
    m_fileMenu->addSeparator();

    m_saveAct = m_fileMenu->addAction(kes::style::icon("save"), tr("&Save"), this, &MainWindow::save);
    m_saveAct->setShortcut(QKeySequence::Save);
    m_saveAsAct = m_fileMenu->addAction(tr("Save &As..."), this, &MainWindow::saveAs);
    m_saveAsAct->setShortcut(QKeySequence::SaveAs);
    m_fileMenu->addAction(tr("Save A&ll"), this, &MainWindow::saveAll);
    m_closeTabAct = m_fileMenu->addAction(tr("&Close Tab"), this, [this]
        { closeTab(m_tabs->currentIndex()); });
    m_closeTabAct->setShortcut(QKeySequence("Ctrl+W"));
    m_fileMenu->addSeparator();

    QAction* openProjectAct = m_fileMenu->addAction(tr("Open &Project..."), this, [this]
        { openProject(); });
    openProjectAct->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_O));
    m_projectSettingsAct = m_fileMenu->addAction(tr("Project &Settings..."), this, [this]
        { if (m_ctx->project().isValid()) ProjectSettingsDialog(m_ctx, this).exec(); });
    m_projectSettingsAct->setToolTip(tr("Edit the name, version, company and startup map in the .kproject"));
    m_projectSettingsAct->setEnabled(m_ctx->project().isValid());
    QAction* contentAct = m_fileMenu->addAction(tr("Set &Content Directory..."), this, &MainWindow::chooseContentDir);
    contentAct->setToolTip(tr("Use a bare content folder instead of a .kproject"));
    m_fileMenu->addSeparator();
    QAction* quitAct = m_fileMenu->addAction(tr("&Quit"), this, &QWidget::close);
    quitAct->setShortcut(QKeySequence::Quit);
}

void MainWindow::createAssetsMenu()
{
    m_assetsMenu = new QMenu(tr("&Assets"), this);
    QAction* import = m_assetsMenu->addAction(tr("&Import..."), this, [this]
        { m_importer->importDialog(); });
    import->setShortcut(QKeySequence(tr("Ctrl+Shift+I")));
    import->setStatusTip(tr("Import models, animations, textures and sounds into their content folders"));

    QMenu* create = m_assetsMenu->addMenu(tr("&New"));
    for (const AssetTemplate& tmpl : assetTemplates())
        create->addAction(tmpl.label, this, [this, tmpl]
            { m_assets->createFromTemplate(tmpl, QString()); });

    QMenu* launch = m_assetsMenu->addMenu(tr("&Launch"));
    connect(launch, &QMenu::aboutToShow, this, [this, launch]
        {
        launch->clear();
        for (const QString &program : AssetEditors::programs())
            launch->addAction(program, this, [this, program]
                              { AssetEditors::launch(m_ctx, this, program); }); });

    m_assetsMenu->addSeparator();
    m_assetsMenu->addAction(tr("Open Content &Folder"), this, [this]
        {
        if (!m_ctx->projectDir().isEmpty())
            QDesktopServices::openUrl(QUrl::fromLocalFile(m_ctx->projectDir())); });
}

void MainWindow::createWindowMenu()
{
    m_windowMenu = new QMenu(tr("&Window"), this);
    connect(m_windowMenu, &QMenu::aboutToShow, this, [this]
        {
        m_windowMenu->clear();
        for (const EditorModule* module : m_modules.modules())
            if (!module->projectToolLabel().isEmpty())
                m_windowMenu->addAction(module->projectToolLabel(), this, [this, module]
                    { openProjectTool(module); });
        if (!m_windowMenu->isEmpty())
            m_windowMenu->addSeparator();
        for (QDockWidget* d : m_sharedDocks)
            m_windowMenu->addAction(d->toggleViewAction());
        if (EditorTab* tab = currentTab())
        {
            const QList<QDockWidget*> docks = tab->findChildren<QDockWidget*>(QString(), Qt::FindDirectChildrenOnly);
            if (!docks.isEmpty())
            {
                m_windowMenu->addSection(tab->displayName());
                for (QDockWidget* d : docks)
                    m_windowMenu->addAction(d->toggleViewAction());
            }
        } });
}

void MainWindow::createDocks()
{
    auto addDock = [this](const QString& title, QWidget* w)
    {
        auto* d = new QDockWidget(title, this);
        d->setObjectName(title);
        d->setWidget(w);
        addDockWidget(Qt::BottomDockWidgetArea, d);
        m_sharedDocks << d;
        return d;
    };

    m_assets = new AssetBrowser(m_ctx, m_importer);
    connect(m_assets, &AssetBrowser::openAssetRequested, this, &MainWindow::openFile);
    m_assets->setPreviewFilter([this](const QString& path)
        { return m_modules.previewModuleFor(path) != nullptr; });
    connect(m_assets, &AssetBrowser::previewAssetRequested, this, &MainWindow::previewFile);
    auto* browser = addDock(tr("Asset Browser"), m_assets);
    auto* console = addDock(tr("Console"), new ConsolePanel(m_ctx));
    tabifyDockWidget(browser, console);

    ProblemsPanel::Shell shell;
    shell.tabs = [this]
    {
        QList<EditorTab*> tabs;
        for (int i = 0; i < m_tabs->count(); ++i)
            if (EditorTab* tab = tabAt(i)) tabs << tab;
        return tabs;
    };
    shell.current = [this] { return currentTab(); };
    shell.activate = [this](EditorTab* tab) { m_tabs->setCurrentWidget(tab); };
    m_problems = new ProblemsPanel(std::move(shell));
    m_problemsDock = addDock(tr("Problems"), m_problems);
    tabifyDockWidget(console, m_problemsDock);
    connect(m_problems, &ProblemsPanel::countChanged, this, [this](int errors, int warnings)
        { m_problemsDock->setWindowTitle(errors + warnings ? tr("Problems (%1)").arg(errors + warnings) : tr("Problems")); });
    browser->raise();
    resizeDocks({ browser }, { 190 }, Qt::Vertical);
}

void MainWindow::rebuildMenuBar()
{
    menuBar()->clear();
    menuBar()->addMenu(m_fileMenu);
    if (EditorTab* tab = currentTab())
        for (QMenu* menu : tab->menus())
            menuBar()->addMenu(menu);
    menuBar()->addMenu(m_assetsMenu);
    menuBar()->addMenu(m_windowMenu);
    menuBar()->addMenu(m_helpMenu);

    const EditorTab* tab = currentTab();
    const EditorModule* module = tab ? m_modules.module(tab->moduleId()) : nullptr;
    m_closeTabAct->setEnabled(module && module->closable());
}

// A floating dock is a window of its own and stays up when its tab is hidden.
void MainWindow::syncFloatingDocks()
{
    static const char* kHiddenWithTab = "forgeHiddenWithTab";
    for (int i = 0; i < m_tabs->count(); ++i)
    {
        EditorTab* tab = tabAt(i);
        const bool current = tab == currentTab();
        for (QDockWidget* d : tab->findChildren<QDockWidget*>(QString(), Qt::FindDirectChildrenOnly))
        {
            if (!current && d->isFloating() && d->isVisible())
            {
                d->setProperty(kHiddenWithTab, true);
                d->hide();
            }
            else if (current && d->property(kHiddenWithTab).toBool())
            {
                d->setProperty(kHiddenWithTab, false);
                d->show();
            }
        }
    }
}

void MainWindow::addTab(EditorTab* tab, bool closable)
{
    const int index = m_tabs->addTab(tab, tab->displayName());
    if (!closable)
    {
        const auto side = QTabBar::ButtonPosition(style()->styleHint(QStyle::SH_TabBar_CloseButtonPosition, nullptr, m_tabs->tabBar()));
        m_tabs->tabBar()->setTabButton(index, side, nullptr);
    }
    connect(tab, &EditorTab::titleChanged, this, &MainWindow::onTabTitleChanged);
    connect(tab, &EditorTab::fileSaved, this, &MainWindow::onFileSaved);
    connect(tab, &EditorTab::openRequested, this, &MainWindow::openFile);
    connect(tab, &EditorTab::problemsChanged, m_problems, &ProblemsPanel::scheduleRefresh);
    connect(tab, &EditorTab::showProblemsRequested, this, [this]
        {
        m_problemsDock->show();
        m_problemsDock->raise(); });
    m_problems->scheduleRefresh();
    updateTabText(tab);
    m_tabs->setCurrentIndex(index);
}

bool MainWindow::closeTab(int index)
{
    EditorTab* tab = tabAt(index);
    if (!tab)
        return false;
    const EditorModule* module = m_modules.module(tab->moduleId());
    if (module && !module->closable())
        return false;
    if (!tab->canClose())
        return false;
    tab->saveLayout();
    m_tabs->removeTab(index);
    tab->disconnect(this);
    tab->deleteLater();
    m_problems->scheduleRefresh();
    return true;
}

void MainWindow::newDocument(const EditorModule* module)
{
    if (!module->allowsMultipleTabs())
    {
        for (int i = 0; i < m_tabs->count(); ++i)
        {
            EditorTab* tab = tabAt(i);
            if (tab->moduleId() != module->id())
                continue;
            m_tabs->setCurrentIndex(i);
            if (tab->canClose())
                tab->newDocument();
            return;
        }
    }
    addTab(module->createTab(m_ctx), module->closable());
}

void MainWindow::openProjectTool(const EditorModule* module)
{
    for (int i = 0; i < m_tabs->count(); ++i)
    {
        if (tabAt(i)->moduleId() == module->id())
        {
            m_tabs->setCurrentIndex(i);
            return;
        }
    }
    const QString project = m_ctx->project().isValid() ? m_ctx->project().filePath : m_ctx->projectDir();
    EditorTab* tab = module->createTab(m_ctx);
    QString error;
    if (!tab->openFile(project, &error))
    {
        delete tab;
        QMessageBox::warning(this, module->projectToolLabel(), error.isEmpty() ? tr("Could not open %1.").arg(project) : error);
        return;
    }
    addTab(tab, module->closable());
}

bool MainWindow::openFile(const QString& path)
{
    const EditorModule* module = m_modules.moduleFor(path);
    if (!module)
    {
        m_ctx->log(tr("KES Editor has no editor for %1.").arg(QFileInfo(path).fileName()), LogLevel::Warning);
        return false;
    }
    for (int i = 0; i < m_tabs->count(); ++i)
    {
        if (tabAt(i)->handlesFile(path))
        {
            m_tabs->setCurrentIndex(i);
            return true;
        }
    }

    EditorTab* tab = nullptr;
    if (!module->allowsMultipleTabs())
        for (int i = 0; i < m_tabs->count() && !tab; ++i)
            if (tabAt(i)->moduleId() == module->id())
                tab = tabAt(i);

    QString error;
    if (tab)
    {
        m_tabs->setCurrentWidget(tab);
        if (!tab->canClose())
            return false;
        if (!tab->openFile(path, &error))
        {
            QMessageBox::critical(this, tr("Open failed"), error);
            return false;
        }
    }
    else
    {
        tab = module->createTab(m_ctx);
        if (!tab->openFile(path, &error))
        {
            delete tab;
            QMessageBox::critical(this, tr("Open failed"), error.isEmpty() ? tr("Could not open %1.").arg(path) : error);
            return false;
        }
        addTab(tab, module->closable());
    }
    addRecentFile(path);
    updateTabText(tab);
    updateTitle();
    return true;
}

bool MainWindow::previewFile(const QString& path)
{
    const EditorModule* module = m_modules.previewModuleFor(path);
    if (!module)
        return false;
    for (int i = 0; i < m_tabs->count(); ++i)
    {
        EditorTab* tab = tabAt(i);
        if (tab->moduleId() == module->id() && tab->handlesFile(path))
        {
            m_tabs->setCurrentIndex(i);
            return true;
        }
    }
    EditorTab* tab = module->createTab(m_ctx);
    QString error;
    if (!tab->openFile(path, &error))
    {
        delete tab;
        QMessageBox::critical(this, tr("Preview failed"), error.isEmpty() ? tr("Could not preview %1.").arg(path) : error);
        return false;
    }
    addTab(tab, module->closable());
    updateTitle();
    return true;
}

void MainWindow::open()
{
    QString dir = m_ctx->projectDir();
    if (EditorTab* tab = currentTab(); tab && !tab->filePath().isEmpty())
        dir = QFileInfo(tab->filePath()).absolutePath();
    const QString f = QFileDialog::getOpenFileName(this, tr("Open"), dir, m_modules.openFilter());
    if (!f.isEmpty())
        openFile(f);
}

void MainWindow::save()
{
    if (EditorTab* tab = currentTab())
        tab->save();
}

void MainWindow::saveAs()
{
    if (EditorTab* tab = currentTab())
        tab->saveAs();
}

void MainWindow::saveAll()
{
    for (int i = 0; i < m_tabs->count(); ++i)
    {
        EditorTab* tab = tabAt(i);
        if (tab->isModified())
        {
            m_tabs->setCurrentIndex(i);
            if (!tab->save())
                return;
        }
    }
}

void MainWindow::onTabTitleChanged()
{
    if (auto* tab = qobject_cast<EditorTab*>(sender()))
        updateTabText(tab);
    updateTitle();
}

void MainWindow::onFileSaved(const QString& path)
{
    addRecentFile(path);
    const QString suffix = QFileInfo(path).suffix().toLower();
    if (suffix == "mat")
        m_ctx->materials()->reload();
    else if (suffix == "kmodel")
        m_ctx->models()->refresh();
    for (int i = 0; i < m_tabs->count(); ++i)
        tabAt(i)->assetSaved(path);
    if (auto* tab = qobject_cast<EditorTab*>(sender()))
        updateTabText(tab);
    updateTitle();
}

void MainWindow::updateTabText(EditorTab* tab)
{
    const int index = m_tabs->indexOf(tab);
    if (index < 0)
        return;
    m_tabs->setTabText(index, tab->displayName() + (tab->isModified() ? "*" : ""));
    m_tabs->setTabToolTip(index, tab->filePath().isEmpty() ? tab->displayName() : QDir::toNativeSeparators(tab->filePath()));
}

void MainWindow::updateTitle()
{
    EditorTab* tab = currentTab();
    const QString doc = tab ? tab->displayName() : tr("KES Editor");
    const QString project = m_ctx->project().isValid() ? m_ctx->project().name : tr("no project");
    setWindowTitle(kes::style::windowTitle(QString("%1 (%2)").arg(doc, project), tr("KES Editor")));
    setWindowModified(tab && tab->isModified());
}

void MainWindow::chooseContentDir()
{
    const QString d = QFileDialog::getExistingDirectory(this, tr("Content Directory"), m_ctx->projectDir());
    if (!d.isEmpty())
        m_ctx->setProjectDir(d);
}

bool MainWindow::openProject(const QString& path)
{
    QString file = path;
    if (file.isEmpty())
    {
        QString startDir = m_ctx->project().isValid() ? QFileInfo(m_ctx->project().filePath).absolutePath() : QString();
#ifdef FORGE_SOURCE_DIR
        if (startDir.isEmpty())
            startDir = QDir::cleanPath(QString(FORGE_SOURCE_DIR) + "/../../game");
#endif
        file = QFileDialog::getOpenFileName(this, tr("Open KES Project"), startDir, tr("KES Project (*.kproject)"));
        if (file.isEmpty())
            return false;
    }
    QString error;
    if (!m_ctx->loadProject(file, &error))
    {
        QMessageBox::critical(this, tr("Open Project"), error);
        return false;
    }
    const KesProject& p = m_ctx->project();
    m_ctx->log(tr("Project: %1 - content %2, maps %3, materials %4, textures %5")
            .arg(p.name, p.assetRoot, p.mapsDir, p.materialsDir, p.texturesDir));
    return true;
}

void MainWindow::promptForProject()
{
    WelcomeDialog welcome(this);
    if (welcome.exec() == QDialog::Accepted)
        openProject(welcome.chosenProject());
}

void MainWindow::closeEvent(QCloseEvent* e)
{
    for (int i = 0; i < m_tabs->count(); ++i)
    {
        EditorTab* tab = tabAt(i);
        if (!tab->isModified())
            continue;
        m_tabs->setCurrentIndex(i);
        if (!tab->canClose())
        {
            e->ignore();
            return;
        }
    }
    for (int i = 0; i < m_tabs->count(); ++i)
        tabAt(i)->saveLayout();
    QSettings s;
    s.setValue("shell/geometry", saveGeometry());
    s.setValue("shell/state", saveState());
    e->accept();
}

void MainWindow::addRecentFile(const QString& path)
{
    QSettings s;
    QStringList files = s.value("recentFiles").toStringList();
    const QString abs = QFileInfo(path).absoluteFilePath();
    files.removeAll(abs);
    files.prepend(abs);
    while (files.size() > 10)
        files.removeLast();
    s.setValue("recentFiles", files);
}

void MainWindow::rebuildRecentMenu()
{
    m_recentMenu->clear();
    const QStringList files = QSettings().value("recentFiles").toStringList();
    int n = 0;
    for (const QString& f : files)
    {
        if (!QFileInfo::exists(f))
            continue;
        QAction* a = m_recentMenu->addAction(QString("&%1  %2").arg(++n).arg(QFileInfo(f).fileName()));
        a->setStatusTip(QDir::toNativeSeparators(f));
        connect(a, &QAction::triggered, this, [this, f]
            { openFile(f); });
    }
    if (n == 0)
    {
        m_recentMenu->addAction(tr("(none)"))->setEnabled(false);
        return;
    }
    m_recentMenu->addSeparator();
    connect(m_recentMenu->addAction(tr("Clear List")), &QAction::triggered, this, []
        { QSettings().remove("recentFiles"); });
}

void MainWindow::dragEnterEvent(QDragEnterEvent* e)
{
    for (const QUrl& u : e->mimeData()->urls())
        if (u.isLocalFile() && (m_modules.moduleFor(u.toLocalFile()) || AssetImporter::canImport(u.toLocalFile())))
        {
            e->acceptProposedAction();
            return;
        }
}

void MainWindow::dropEvent(QDropEvent* e)
{
    QStringList sources;
    bool handled = false;
    for (const QUrl& u : e->mimeData()->urls())
    {
        if (!u.isLocalFile())
            continue;
        const QString path = u.toLocalFile();
        if (m_modules.moduleFor(path))
        {
            openFile(path);
            handled = true;
        }
        else if (AssetImporter::canImport(path))
        {
            sources << path;
        }
    }
    if (!sources.isEmpty())
    {
        m_importer->import(sources);
        handled = true;
    }
    if (handled)
        e->acceptProposedAction();
}
