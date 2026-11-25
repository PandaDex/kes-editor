#include "LocalizationTab.h"
#include "DetailsPanel.h"
#include "Languages.h"
#include "MissingKeysPanel.h"
#include "StringTableModel.h"
#include "core/EditorContext.h"
#include "ui/KesStyle.h"

#include <QApplication>
#include <QComboBox>
#include <QCryptographicHash>
#include <QDir>
#include <QDockWidget>
#include <QFileInfo>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QSet>
#include <QSettings>
#include <QStatusBar>
#include <QTableView>
#include <QToolBar>
#include <QToolButton>
#include <QUndoStack>

namespace kle
{

    namespace
    {

        constexpr int kSnapshotCommandId = 1;
        const char* kTool = "Localization";

        class SnapshotCommand : public QUndoCommand
        {
        public:
            SnapshotCommand(LocalizationTab* tab, LocProject before, LocProject after, const QString& text, const QString& mergeKey)
                : QUndoCommand(text), tab_(tab), before_(std::move(before)), after_(std::move(after)), mergeKey_(mergeKey) {}

            void undo() override { tab_->setProjectState(before_); }
            void redo() override { tab_->setProjectState(after_); }
            int id() const override { return mergeKey_.isEmpty() ? -1 : kSnapshotCommandId; }
            bool mergeWith(const QUndoCommand* other) override
            {
                const auto* o = static_cast<const SnapshotCommand*>(other);
                if (o->mergeKey_ != mergeKey_) return false;
                after_ = o->after_;
                return true;
            }

        private:
            LocalizationTab* tab_;
            LocProject before_;
            LocProject after_;
            QString mergeKey_;
        };

        bool samePath(const QString& a, const QString& b)
        {
            return !a.isEmpty() && QDir::cleanPath(QFileInfo(a).absoluteFilePath()).compare(QDir::cleanPath(QFileInfo(b).absoluteFilePath()), Qt::CaseInsensitive) == 0;
        }

    } // namespace

    LocalizationTab::LocalizationTab(EditorContext* ctx, QWidget* parent)
        : EditorTab(ctx, QStringLiteral("localization"), parent)
    {
        undo_ = new QUndoStack(this);
        model_ = new StringTableModel(this);
        filter_ = new StringFilterModel(this);
        filter_->setSourceModel(model_);
        filter_->setSortRole(Qt::EditRole);
        filter_->setSortCaseSensitivity(Qt::CaseInsensitive);

        details_ = new DetailsPanel(this);
        buildTable();

        auto* detailsDock = new QDockWidget(QStringLiteral("Details"), this);
        detailsDock->setObjectName(QStringLiteral("DetailsDock"));
        detailsDock->setWidget(details_);
        addDockWidget(Qt::RightDockWidgetArea, detailsDock);

        missing_ = new MissingKeysPanel(this);
        auto* missingDock = new QDockWidget(QStringLiteral("Missing Keys"), this);
        missingDock->setObjectName(QStringLiteral("MissingDock"));
        missingDock->setWidget(missing_);
        addDockWidget(Qt::BottomDockWidgetArea, missingDock);
        resizeDocks({ detailsDock }, { 420 }, Qt::Horizontal);
        resizeDocks({ missingDock }, { 170 }, Qt::Vertical);

        statusLabel_ = new QLabel(this);
        progressLabel_ = new QLabel(this);
        statusBar()->addWidget(statusLabel_, 1);
        statusBar()->addPermanentWidget(progressLabel_);

        buildActions();

        connect(undo_, &QUndoStack::cleanChanged, this, &EditorTab::titleChanged);

        connect(model_, &StringTableModel::translationEdited, this, [this](const QString& code, const QString& key, const QString& text)
            { applyEdit(QStringLiteral("Translate #%1 (%2)").arg(key, code), {}, [&](LocProject& p)
                  {
            p.setTranslation(code, key, {text});
            p.setFuzzy(code, key, false); }); });

        connect(details_, &DetailsPanel::renameRequested, this, [this](const QString& from, const QString& to)
            {
        applyEdit(QStringLiteral("Rename #%1 to #%2").arg(from, to), {}, [&](LocProject& p) { p.renameKey(from, to); });
        selectKey(to); });
        connect(details_, &DetailsPanel::noteEdited, this, [this](const QString& key, const QString& note)
            { applyEdit(QStringLiteral("Edit note of #%1").arg(key), QStringLiteral("note|") + key, [&](LocProject& p)
                  { p.setNote(key, note); }); });
        connect(details_, &DetailsPanel::pluralToggled, this, [this](const QString& key, bool plural)
            { applyEdit(plural ? QStringLiteral("Make #%1 plural").arg(key) : QStringLiteral("Make #%1 singular").arg(key), {},
                  [&](LocProject& p)
                  { p.setPlural(key, plural); }); });
        connect(details_, &DetailsPanel::translationEdited, this, [this](const QString& code, const QString& key, const QStringList& forms)
            { applyEdit(QStringLiteral("Translate #%1 (%2)").arg(key, code), QStringLiteral("tr|%1|%2").arg(code, key),
                  [&](LocProject& p)
                  { p.setTranslation(code, key, forms); }); });
        connect(details_, &DetailsPanel::fuzzyToggled, this, [this](const QString& code, const QString& key, bool fuzzy)
            { applyEdit(fuzzy ? QStringLiteral("Mark #%1 fuzzy (%2)").arg(key, code) : QStringLiteral("Approve #%1 (%2)").arg(key, code), {},
                  [&](LocProject& p)
                  { p.setFuzzy(code, key, fuzzy); }); });

        connect(missing_, &MissingKeysPanel::addRequested, this, &LocalizationTab::addKeys);
        connect(missing_, &MissingKeysPanel::rescanRequested, this, &LocalizationTab::rescan);

        refreshProject();
        updateActions();
        restoreLayout();
    }

    void LocalizationTab::buildTable()
    {
        table_ = new QTableView(this);
        table_->setModel(filter_);
        table_->setSortingEnabled(true);
        table_->sortByColumn(StringTableModel::KeyColumn, Qt::AscendingOrder);
        table_->setSelectionBehavior(QAbstractItemView::SelectItems);
        table_->setSelectionMode(QAbstractItemView::ExtendedSelection);
        table_->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed | QAbstractItemView::AnyKeyPressed);
        table_->setWordWrap(false);
        table_->setTextElideMode(Qt::ElideRight);
        table_->setAlternatingRowColors(true);
        table_->verticalHeader()->setVisible(false);
        table_->verticalHeader()->setDefaultSectionSize(table_->fontMetrics().height() + 10);
        table_->horizontalHeader()->setSectionsMovable(true);
        table_->horizontalHeader()->setHighlightSections(false);
        table_->horizontalHeader()->setDefaultSectionSize(300);
        table_->horizontalHeader()->setMinimumSectionSize(40);
        setCentralWidget(table_);

        connect(table_->selectionModel(), &QItemSelectionModel::currentChanged, this, [this]
            {
        ++mergeSession_;
        details_->setKey(currentKey()); });
        connect(table_, &QTableView::doubleClicked, this, [this](const QModelIndex& index)
            {
        if (index.flags() & Qt::ItemIsEditable) return;
        const QString code = model_->languageAt(index.column());
        if (!code.isEmpty()) details_->focusLanguage(code); });
        connect(table_->horizontalHeader(), &QHeaderView::sectionMoved, this, [this]
            {
        if (!applyingLayout_) readVisibleFromHeader(); });
    }

    void LocalizationTab::buildActions()
    {
        QAction* undo = undo_->createUndoAction(this, QStringLiteral("&Undo"));
        undo->setShortcut(QKeySequence::Undo);
        undo->setIcon(kes::style::icon(QStringLiteral("undo")));
        QAction* redo = undo_->createRedoAction(this, QStringLiteral("&Redo"));
        redo->setShortcuts({ QKeySequence::Redo, QKeySequence("Ctrl+Y") });
        redo->setIcon(kes::style::icon(QStringLiteral("redo")));

        actReload_ = new QAction(QStringLiteral("&Reload from Disk"), this);
        actAddKey_ = new QAction(kes::style::icon(QStringLiteral("plus")), QStringLiteral("&Add Key..."), this);
        actAddKey_->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_N));
        actDeleteKey_ = new QAction(kes::style::icon(QStringLiteral("trash")), QStringLiteral("&Delete Selected Keys"), this);
        actDeleteKey_->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Delete));
        actAddLanguage_ = new QAction(QStringLiteral("Add &Language..."), this);
        actRescan_ = new QAction(kes::style::icon(QStringLiteral("search")), QStringLiteral("&Rescan Project"), this);
        actRescan_->setShortcut(QKeySequence(Qt::Key_F5));
        actRescan_->setToolTip(QStringLiteral("Find #Keys used in content and C++ source (F5)"));
        actSettings_ = new QAction(QStringLiteral("Localization &Settings..."), this);
        actSettings_->setToolTip(QStringLiteral("Source language, extra scan folders and C++ functions that take keys"));
        actFind_ = new QAction(QStringLiteral("&Find"), this);
        actFind_->setShortcut(QKeySequence::Find);
        actNext_ = new QAction(kes::style::icon(QStringLiteral("down")), QStringLiteral("&Next Needs Work"), this);
        actNext_->setShortcut(QKeySequence(Qt::Key_F3));
        actNext_->setToolTip(QStringLiteral("Next key that is untranslated or fuzzy in a visible language (F3)"));

        connect(actReload_, &QAction::triggered, this, &LocalizationTab::reload);
        connect(actAddKey_, &QAction::triggered, this, [this]
            { addKey(); });
        connect(actDeleteKey_, &QAction::triggered, this, &LocalizationTab::deleteSelectedKeys);
        connect(actAddLanguage_, &QAction::triggered, this, &LocalizationTab::addLanguage);
        connect(actRescan_, &QAction::triggered, this, &LocalizationTab::rescan);
        connect(actSettings_, &QAction::triggered, this, &LocalizationTab::editProjectSettings);
        connect(actNext_, &QAction::triggered, this, &LocalizationTab::nextNeedsWork);

        QMenu* edit = addTabMenu(QStringLiteral("&Edit"));
        edit->addAction(undo);
        edit->addAction(redo);
        edit->addSeparator();
        edit->addAction(actAddKey_);
        edit->addAction(actDeleteKey_);
        edit->addSeparator();
        edit->addAction(actFind_);
        edit->addAction(actNext_);

        QMenu* loc = addTabMenu(QStringLiteral("&Localization"));
        loc->addAction(actAddLanguage_);
        loc->addAction(actRescan_);
        loc->addSeparator();
        loc->addAction(actReload_);
        loc->addAction(actSettings_);

        QMenu* view = addTabMenu(QStringLiteral("&View"));
        languagesMenu_ = view->addMenu(QStringLiteral("&Languages"));
        view->addSeparator();
        for (QDockWidget* d : findChildren<QDockWidget*>(QString(), Qt::FindDirectChildrenOnly))
            view->addAction(d->toggleViewAction());

        QToolBar* tb = addToolBar(QStringLiteral("Localization"));
        tb->setObjectName(QStringLiteral("LocalizationToolbar"));
        tb->setMovable(false);
        tb->setIconSize(QSize(18, 18));
        tb->addAction(undo);
        tb->addAction(redo);
        tb->addSeparator();
        tb->addAction(actAddKey_);
        tb->addAction(actRescan_);
        tb->addAction(actNext_);
        tb->addSeparator();

        languagesButton_ = new QToolButton(this);
        languagesButton_->setText(QStringLiteral("Languages"));
        languagesButton_->setToolTip(QStringLiteral("Language columns to show. Drag column headers to reorder them."));
        languagesButton_->setMenu(languagesMenu_);
        languagesButton_->setPopupMode(QToolButton::InstantPopup);
        tb->addWidget(languagesButton_);
        tb->addSeparator();

        filterMode_ = new QComboBox(this);
        filterMode_->addItem(QStringLiteral("All keys"), int(StringFilterModel::Mode::All));
        filterMode_->addItem(QStringLiteral("Needs work"), int(StringFilterModel::Mode::NeedsWork));
        filterMode_->addItem(QStringLiteral("Untranslated"), int(StringFilterModel::Mode::Untranslated));
        filterMode_->addItem(QStringLiteral("Fuzzy"), int(StringFilterModel::Mode::Fuzzy));
        filterMode_->addItem(QStringLiteral("Unused in project"), int(StringFilterModel::Mode::Unused));
        filterMode_->setToolTip(QStringLiteral("\"Needs work\", \"Untranslated\" and \"Fuzzy\" look at the visible languages"));
        tb->addWidget(filterMode_);

        search_ = new QLineEdit(this);
        search_->setPlaceholderText(QStringLiteral("Search keys and visible text (Ctrl+F)"));
        search_->setClearButtonEnabled(true);
        search_->setMaximumWidth(340);
        search_->addAction(kes::style::icon(QStringLiteral("search")), QLineEdit::LeadingPosition);
        tb->addWidget(search_);

        connect(filterMode_, &QComboBox::currentIndexChanged, this, [this]
            {
        filter_->setMode(StringFilterModel::Mode(filterMode_->currentData().toInt()));
        updateStatus(); });
        connect(search_, &QLineEdit::textChanged, this, [this](const QString& text)
            {
        filter_->setText(text);
        updateStatus(); });
        connect(actFind_, &QAction::triggered, this, [this]
            {
        search_->setFocus();
        search_->selectAll(); });
    }

    QString LocalizationTab::filePath() const
    {
        return paths_.projectFile.isEmpty() ? paths_.localizationDir : paths_.projectFile;
    }

    QString LocalizationTab::displayName() const
    {
        return QStringLiteral("Localization");
    }

    bool LocalizationTab::isModified() const
    {
        return loaded_ && !undo_->isClean();
    }

    bool LocalizationTab::handlesFile(const QString& path) const
    {
        if (!loaded_) return false;
        if (samePath(path, paths_.projectFile)) return true;
        const QString rel = QDir(paths_.localizationDir).relativeFilePath(QFileInfo(path).absoluteFilePath());
        return !rel.startsWith(QLatin1String("..")) && !QDir::isAbsolutePath(rel);
    }

    bool LocalizationTab::openFile(const QString& path, QString* error)
    {
        return openProject(path, error);
    }

    void LocalizationTab::projectChanged()
    {
        const QString project = context()->project().isValid() ? context()->project().filePath : context()->projectDir();
        const ProjectPaths paths = findProject(project);
        if (!paths.isValid() || (samePath(paths.localizationDir, paths_.localizationDir))) return;
        if (!canClose()) return;
        undo_->setClean();
        QString error;
        if (!openProject(project, &error)) showStatus(error, 8000);
    }

    bool LocalizationTab::openProject(const QString& path, QString* error)
    {
        const ProjectPaths paths = findProject(path);
        if (!paths.isValid())
        {
            if (error)
                *error = QStringLiteral("No KES project found for\n%1\n\nOpen a .kproject file or a folder inside a game project.")
                             .arg(QDir::toNativeSeparators(path));
            return false;
        }

        paths_ = paths;
        loadProjectSettings();

        QStringList errors;
        LocProject loaded;
        loaded.load(paths_.localizationDir, settings_.sourceLanguage, &errors);
        if (!errors.isEmpty())
            QMessageBox::warning(this, QString::fromLatin1(kTool),
                QStringLiteral("Some files could not be read and are not shown (they won't be changed):\n\n%1")
                    .arg(errors.join(QLatin1Char('\n'))));

        project_ = loaded;
        loaded_ = true;
        undo_->clear();
        scanned_ = false;
        scan_ = {};

        model_->setProject(&project_);
        model_->setScan(nullptr);
        filter_->setScanned(false);
        details_->setProject(&project_, nullptr, paths_.projectDir);

        QSettings s;
        s.beginGroup(settingsGroup());
        QStringList visible = s.value(QStringLiteral("visibleLanguages")).toStringList();
        s.endGroup();
        QStringList codes = project_.languageCodes();
        visible.erase(std::remove_if(visible.begin(), visible.end(), [&](const QString& c)
                          { return !codes.contains(c); }),
            visible.end());
        if (visible.isEmpty()) visible = codes;
        knownLanguages_ = codes;
        setVisibleLanguages(visible);

        refreshProject();
        rescan();
        updateActions();
        emit titleChanged();
        if (model_->rowCount() > 0) table_->setCurrentIndex(filter_->index(0, StringTableModel::KeyColumn));

        // a .po opens on its language
        const QFileInfo info(path);
        if (info.suffix().compare(QLatin1String("po"), Qt::CaseInsensitive) == 0 && codes.contains(info.completeBaseName()))
        {
            if (!visible_.contains(info.completeBaseName())) setVisibleLanguages(visible_ + QStringList{ info.completeBaseName() });
            details_->focusLanguage(info.completeBaseName());
        }
        return true;
    }

    void LocalizationTab::setProjectState(const LocProject& state)
    {
        project_ = state;
        refreshProject();
    }

    void LocalizationTab::applyEdit(const QString& text, const QString& mergeKey, const std::function<void(LocProject&)>& edit)
    {
        if (!loaded_) return;
        LocProject after = project_;
        edit(after);
        const QString merge = mergeKey.isEmpty() ? QString() : QStringLiteral("%1|%2").arg(mergeSession_).arg(mergeKey);
        undo_->push(new SnapshotCommand(this, project_, after, text, merge));
    }

    void LocalizationTab::refreshProject()
    {
        const QString key = currentKey();
        const QStringList codes = project_.languageCodes();
        model_->projectChanged();

        // New languages (added or undone) show up as visible columns.
        QStringList visible = visible_;
        visible.erase(std::remove_if(visible.begin(), visible.end(), [&](const QString& c)
                          { return !codes.contains(c); }),
            visible.end());
        for (const QString& c : codes)
            if (!visible.contains(c) && !knownLanguages_.contains(c)) visible << c;
        knownLanguages_ = codes;
        if (visible != visible_)
            setVisibleLanguages(visible);
        else
            applyColumnLayout();

        if (!key.isEmpty() && currentKey() != key && project_.hasKey(key)) selectKey(key);
        details_->setKey(currentKey());
        details_->refresh();
        missing_->refresh(loaded_ ? &project_ : nullptr, scanned_ ? &scan_ : nullptr, paths_.projectDir);
        updateStatus();
    }

    void LocalizationTab::rescan()
    {
        if (!loaded_) return;
        QApplication::setOverrideCursor(Qt::WaitCursor);
        ScanSettings s;
        s.baseFolder = paths_.projectDir;
        s.folders << paths_.projectDir;
        for (const QString& folder : settings_.extraFolders)
            s.folders << QDir(paths_.projectDir).absoluteFilePath(folder);
        if (!settings_.cppFunctions.isEmpty()) s.cppFunctions = settings_.cppFunctions;
        scan_ = scanForKeys(s);
        scanned_ = true;
        QApplication::restoreOverrideCursor();

        model_->setScan(&scan_);
        filter_->setScanned(true);
        details_->setProject(&project_, &scan_, paths_.projectDir);
        details_->setLanguages(visible_);
        details_->setKey(currentKey());
        missing_->refresh(&project_, &scan_, paths_.projectDir);
        showStatus(QStringLiteral("Scanned %1 files, %2 keys used.").arg(scan_.filesScanned).arg(scan_.references.size()), 5000);
        updateStatus();
    }

    void LocalizationTab::setVisibleLanguages(const QStringList& codes)
    {
        visible_ = codes;
        applyColumnLayout();
        filter_->setVisibleLanguages(visible_);
        details_->setLanguages(visible_);
        updateLanguageMenu();
        saveProjectSettings();
        updateStatus();
    }

    void LocalizationTab::applyColumnLayout()
    {
        applyingLayout_ = true;
        QHeaderView* header = table_->horizontalHeader();
        QList<int> order = { StringTableModel::KeyColumn, StringTableModel::UsesColumn };
        for (const QString& code : visible_)
        {
            const int column = model_->columnOf(code);
            if (column >= 0) order << column;
        }
        for (int c = 0; c < model_->columnCount(); ++c)
        {
            if (!order.contains(c)) order << c;
            table_->setColumnHidden(c, c >= StringTableModel::FirstLanguageColumn && !visible_.contains(model_->languageAt(c)));
        }
        for (int visual = 0; visual < order.size(); ++visual)
        {
            const int current = header->visualIndex(order[visual]);
            if (current != visual && current >= 0) header->moveSection(current, visual);
        }
        if (header->sectionSize(StringTableModel::KeyColumn) == header->defaultSectionSize())
        {
            header->resizeSection(StringTableModel::KeyColumn, 240);
            header->resizeSection(StringTableModel::UsesColumn, 56);
        }
        applyingLayout_ = false;
    }

    void LocalizationTab::readVisibleFromHeader()
    {
        QHeaderView* header = table_->horizontalHeader();
        QList<QPair<int, QString>> ordered;
        for (const QString& code : visible_)
            ordered << qMakePair(header->visualIndex(model_->columnOf(code)), code);
        std::sort(ordered.begin(), ordered.end());
        QStringList codes;
        for (const auto& p : ordered)
            codes << p.second;
        if (codes == visible_) return;
        visible_ = codes;
        details_->setLanguages(visible_);
        saveProjectSettings();
    }

    void LocalizationTab::updateLanguageMenu()
    {
        languagesMenu_->clear();
        for (const QString& code : project_.languageCodes())
        {
            QAction* a = languagesMenu_->addAction(QStringLiteral("%1 (%2)").arg(languageName(code), code));
            a->setCheckable(true);
            a->setChecked(visible_.contains(code));
            connect(a, &QAction::toggled, this, [this, code](bool on)
                {
            QStringList v = visible_;
            if (on && !v.contains(code)) v << code;
            if (!on) v.removeAll(code);
            setVisibleLanguages(v); });
        }
        languagesMenu_->addSeparator();
        languagesMenu_->addAction(QStringLiteral("Show All"), this, [this]
            {
        QStringList v = visible_;
        for (const QString& c : project_.languageCodes())
            if (!v.contains(c)) v << c;
        setVisibleLanguages(v); });
        languagesMenu_->addAction(actAddLanguage_);
        languagesButton_->setText(QStringLiteral("Languages (%1/%2)").arg(visible_.size()).arg(project_.languages().size()));
    }

    void LocalizationTab::updateActions()
    {
        for (QAction* a : { actReload_, actAddKey_, actDeleteKey_, actAddLanguage_, actRescan_, actSettings_, actNext_ })
            a->setEnabled(loaded_);
    }

    void LocalizationTab::updateStatus()
    {
        if (!loaded_)
        {
            statusLabel_->setText(QStringLiteral("No project loaded."));
            progressLabel_->clear();
            return;
        }
        QString text = QStringLiteral("%1 keys").arg(model_->rowCount());
        if (filter_->rowCount() != model_->rowCount()) text += QString::fromUtf8(" · %1 shown").arg(filter_->rowCount());
        text += QString::fromUtf8(" · %1").arg(QDir::toNativeSeparators(paths_.localizationDir));
        if (scanned_ && missing_->missingCount() > 0)
            text += QString::fromUtf8(" · <span style=\"color:%1\">%2 used keys missing</span>").arg(kes::style::warning().name()).arg(missing_->missingCount());
        statusLabel_->setText(text);

        QStringList progress;
        const int total = model_->rowCount();
        for (const QString& code : visible_)
        {
            if (code == project_.sourceLanguage() || total == 0) continue;
            progress << QStringLiteral("%1 %2%").arg(code).arg(model_->translatedCount(code) * 100 / total);
        }
        progressLabel_->setText(progress.join(QStringLiteral("   ")));
    }

    QString LocalizationTab::settingsGroup() const
    {
        const QString id = paths_.projectFile.isEmpty() ? paths_.contentRoot : paths_.projectFile;
        return QStringLiteral("localization/projects/") +
            QString::fromLatin1(QCryptographicHash::hash(QDir::cleanPath(id).toLower().toUtf8(), QCryptographicHash::Md5).toHex());
    }

    void LocalizationTab::loadProjectSettings()
    {
        QSettings s;
        s.beginGroup(settingsGroup());
        settings_ = ProjectSettings{};
        settings_.sourceLanguage = s.value(QStringLiteral("sourceLanguage"), QStringLiteral("en")).toString();
        settings_.extraFolders = s.value(QStringLiteral("extraFolders")).toStringList();
        settings_.cppFunctions = s.value(QStringLiteral("cppFunctions"), ScanSettings{}.cppFunctions).toStringList();
        s.endGroup();
    }

    void LocalizationTab::saveProjectSettings() const
    {
        if (!loaded_) return;
        QSettings s;
        s.beginGroup(settingsGroup());
        s.setValue(QStringLiteral("sourceLanguage"), settings_.sourceLanguage);
        s.setValue(QStringLiteral("extraFolders"), settings_.extraFolders);
        s.setValue(QStringLiteral("cppFunctions"), settings_.cppFunctions);
        s.setValue(QStringLiteral("visibleLanguages"), visible_);
        s.endGroup();
    }

    bool LocalizationTab::save()
    {
        if (!loaded_) return false;
        QString error;
        const QStringList written = project_.save(&error);
        if (!error.isEmpty())
        {
            QMessageBox::critical(this, QString::fromLatin1(kTool), error);
            return false;
        }
        undo_->setClean();
        QStringList names;
        for (const QString& path : written)
            names << QFileInfo(path).fileName();
        showStatus(written.isEmpty() ? QStringLiteral("Nothing to save.") : QStringLiteral("Saved %1.").arg(names.join(QStringLiteral(", "))), 5000);
        emit titleChanged();
        if (!written.isEmpty()) emit fileSaved(written.first());
        return true;
    }

    void LocalizationTab::reload()
    {
        if (!loaded_) return;
        if (!undo_->isClean() &&
            QMessageBox::question(this, QString::fromLatin1(kTool), QStringLiteral("Discard unsaved changes and reload the files from disk?")) !=
                QMessageBox::Yes)
            return;
        undo_->setClean();
        QString error;
        if (!openProject(paths_.projectFile.isEmpty() ? paths_.localizationDir : paths_.projectFile, &error))
            QMessageBox::warning(this, QString::fromLatin1(kTool), error);
    }

    void LocalizationTab::addKey(const QString& prefill)
    {
        if (!loaded_) return;
        AddKeyDialog dialog(project_, prefill, this);
        if (dialog.exec() != QDialog::Accepted) return;
        const QString key = dialog.key();
        const QString text = dialog.sourceText();
        const QString plural = dialog.sourcePlural();
        applyEdit(QStringLiteral("Add #%1").arg(key), {}, [&](LocProject& p)
            { p.addKey(key, text, plural); });
        filterMode_->setCurrentIndex(0);
        selectKey(key);
    }

    void LocalizationTab::addKeys(const QStringList& keys)
    {
        if (!loaded_ || keys.isEmpty()) return;
        if (keys.size() == 1)
        {
            addKey(keys.first());
            return;
        }
        applyEdit(QStringLiteral("Add %1 keys").arg(keys.size()), {}, [&](LocProject& p)
            {
        for (const QString& key : keys) p.addKey(key, {}); });
        selectKey(keys.first());
        showStatus(QStringLiteral("Added %1 keys with empty %2 text. Fill them in (filter: Untranslated).")
                       .arg(keys.size())
                       .arg(languageName(project_.sourceLanguage())),
            8000);
    }

    void LocalizationTab::deleteSelectedKeys()
    {
        QStringList keys;
        for (const QModelIndex& index : table_->selectionModel()->selectedIndexes())
        {
            const QString key = index.data(StringTableModel::KeyRole).toString();
            if (!key.isEmpty() && !keys.contains(key)) keys << key;
        }
        if (keys.isEmpty()) return;
        const QString what = keys.size() == 1 ? QStringLiteral("#%1").arg(keys.first()) : QStringLiteral("%1 keys").arg(keys.size());
        if (QMessageBox::question(this, QString::fromLatin1(kTool), QStringLiteral("Delete %1 from every language?").arg(what)) != QMessageBox::Yes)
            return;
        applyEdit(QStringLiteral("Delete %1").arg(what), {}, [&](LocProject& p)
            {
        for (const QString& key : keys) p.removeKey(key); });
    }

    void LocalizationTab::addLanguage()
    {
        if (!loaded_) return;
        AddLanguageDialog dialog(project_.languageCodes(), this);
        if (dialog.exec() != QDialog::Accepted) return;
        const QString code = dialog.code();
        QString error;
        LocProject check = project_;
        if (!check.addLanguage(code, &error))
        {
            QMessageBox::warning(this, QString::fromLatin1(kTool), error);
            return;
        }
        applyEdit(QStringLiteral("Add language %1").arg(code), {}, [&](LocProject& p)
            { p.addLanguage(code); });
        showStatus(QStringLiteral("Added %1. It is written to localization/%2.po on save.").arg(languageName(code), code), 8000);
    }

    void LocalizationTab::editProjectSettings()
    {
        if (!loaded_) return;
        ProjectSettingsDialog dialog(settings_, this);
        if (dialog.exec() != QDialog::Accepted) return;
        const ProjectSettings updated = dialog.settings();
        const bool sourceChanged = updated.sourceLanguage != settings_.sourceLanguage;
        settings_ = updated;
        saveProjectSettings();
        if (sourceChanged)
        {
            QMessageBox::information(this, QString::fromLatin1(kTool),
                QStringLiteral("The source language changed; the project will be reloaded.\n"
                               "Remember to call LocalizationSetSourceLanguage(\"%1\") in the game.")
                    .arg(settings_.sourceLanguage));
            reload();
            return;
        }
        rescan();
    }

    void LocalizationTab::nextNeedsWork()
    {
        const int rows = filter_->rowCount();
        if (rows == 0) return;
        const int start = table_->currentIndex().isValid() ? table_->currentIndex().row() : -1;
        for (int step = 1; step <= rows; ++step)
        {
            const int row = (start + step) % rows;
            for (const QString& code : visible_)
            {
                const int column = model_->columnOf(code);
                const QModelIndex index = filter_->index(row, column);
                const auto status = CellStatus(index.data(StringTableModel::StatusRole).toInt());
                if (status == CellStatus::Translated) continue;
                table_->setCurrentIndex(index);
                table_->scrollTo(index);
                details_->focusLanguage(code);
                return;
            }
        }
        showStatus(QStringLiteral("Every shown key is translated in the visible languages."), 5000);
    }

    QString LocalizationTab::currentKey() const
    {
        return table_ ? table_->currentIndex().data(StringTableModel::KeyRole).toString() : QString();
    }

    void LocalizationTab::selectKey(const QString& key)
    {
        const int sourceRow = model_->rowOf(key);
        if (sourceRow < 0) return;
        const QModelIndex index = filter_->mapFromSource(model_->index(sourceRow, StringTableModel::KeyColumn));
        if (!index.isValid()) return;
        table_->setCurrentIndex(index);
        table_->scrollTo(index);
    }

} // namespace kle
