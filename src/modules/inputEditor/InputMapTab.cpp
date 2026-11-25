#include "InputMapTab.h"
#include "ActionDetailsPanel.h"
#include "ActionListPanel.h"
#include "InputDocument.h"
#include "PreviewPanel.h"
#include "ProblemsPanel.h"
#include "Validator.h"
#include "core/EditorContext.h"
#include "ui/KesStyle.h"

#include <QDir>
#include <QDockWidget>
#include <QFileDialog>
#include <QFileInfo>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QStatusBar>
#include <QToolBar>
#include <QUndoStack>

namespace imap
{
    namespace
    {
        const char* kFilter = "Input action maps (*.imap);;All files (*)";
    }

    InputMapTab::InputMapTab(EditorContext* ctx, QWidget* parent)
        : EditorTab(ctx, QStringLiteral("inputMap"), parent)
    {
        doc_ = new InputDocument(this);

        list_ = new ActionListPanel(doc_, this);
        details_ = new ActionDetailsPanel(doc_, this);
        preview_ = new PreviewPanel(this);
        problems_ = new ProblemsPanel(this);

        setCentralWidget(details_);

        auto* listDock = new QDockWidget(tr("Input Actions"), this);
        listDock->setObjectName(QStringLiteral("ActionsDock"));
        listDock->setWidget(list_);
        addDockWidget(Qt::LeftDockWidgetArea, listDock);

        auto* previewDock = new QDockWidget(tr("Generated .imap"), this);
        previewDock->setObjectName(QStringLiteral("PreviewDock"));
        previewDock->setWidget(preview_);
        addDockWidget(Qt::RightDockWidgetArea, previewDock);

        problemsDock_ = new QDockWidget(tr("Problems"), this);
        problemsDock_->setObjectName(QStringLiteral("ProblemsDock"));
        problemsDock_->setWidget(problems_);
        addDockWidget(Qt::BottomDockWidgetArea, problemsDock_);

        resizeDocks({ listDock, previewDock }, { 290, 400 }, Qt::Horizontal);
        resizeDocks({ problemsDock_ }, { 120 }, Qt::Vertical);

        statusLabel_ = new QLabel(this);
        issueLabel_ = new QLabel(this);
        statusBar()->addWidget(statusLabel_, 1);
        statusBar()->addPermanentWidget(issueLabel_);

        buildActions();

        connect(list_, &ActionListPanel::currentChanged, this, [this](int idx)
            {
            details_->setActionIndex(idx);
            const InputMap& m = doc_->map();
            preview_->highlightAction(idx >= 0 && idx < m.actions.size() ? m.actions[idx].name : QString(), true); });
        connect(problems_, &ProblemsPanel::locate, this, [this](int action, int binding)
            {
            list_->setCurrentIndex(action);
            if (binding >= 0) details_->revealBinding(binding); });

        connect(doc_, &InputDocument::structureChanged, this, &InputMapTab::onDocumentChanged);
        connect(doc_, &InputDocument::contentChanged, this, &InputMapTab::onDocumentChanged);
        connect(doc_, &InputDocument::modifiedChanged, this, &EditorTab::titleChanged);
        connect(doc_, &InputDocument::pathChanged, this, &EditorTab::titleChanged);

        onDocumentChanged();
        restoreLayout();
    }

    InputMapTab::~InputMapTab()
    {
        doc_->disconnect(this);
    }

    void InputMapTab::buildActions()
    {
        QAction* undo = doc_->undoStack()->createUndoAction(this, tr("&Undo"));
        undo->setShortcut(QKeySequence::Undo);
        undo->setIcon(kes::style::icon(QStringLiteral("undo")));
        QAction* redo = doc_->undoStack()->createRedoAction(this, tr("&Redo"));
        redo->setShortcuts({ QKeySequence::Redo, QKeySequence("Ctrl+Y") });
        redo->setIcon(kes::style::icon(QStringLiteral("redo")));

        auto* add = new QAction(kes::style::icon(QStringLiteral("plus")), tr("New Input &Action"), this);
        add->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Return));
        auto* dup = new QAction(kes::style::icon(QStringLiteral("copy")), tr("&Duplicate Action"), this);
        dup->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_D));
        auto* del = new QAction(kes::style::icon(QStringLiteral("trash")), tr("D&elete Action"), this);
        connect(add, &QAction::triggered, list_, &ActionListPanel::addAction);
        connect(dup, &QAction::triggered, list_, &ActionListPanel::duplicateCurrent);
        connect(del, &QAction::triggered, list_, &ActionListPanel::deleteCurrent);

        QMenu* edit = addTabMenu(tr("&Edit"));
        edit->addAction(undo);
        edit->addAction(redo);
        edit->addSeparator();
        edit->addAction(add);
        edit->addAction(dup);
        edit->addAction(del);

        QMenu* view = addTabMenu(tr("&View"));
        for (QDockWidget* d : findChildren<QDockWidget*>(QString(), Qt::FindDirectChildrenOnly))
            view->addAction(d->toggleViewAction());

        QToolBar* tb = addToolBar(tr("Input Map"));
        tb->setObjectName(QStringLiteral("InputMapToolbar"));
        tb->setMovable(false);
        tb->setIconSize(QSize(18, 18));
        tb->addAction(undo);
        tb->addAction(redo);
        tb->addSeparator();
        tb->addAction(add);
    }

    void InputMapTab::onDocumentChanged()
    {
        const InputMap& map = doc_->map();
        const QVector<Issue> issues = validate(map, doc_->loadWarnings());

        QStringList names;
        int mappings = 0;
        for (const Action& a : map.actions)
        {
            names << a.name;
            mappings += a.bindings.size();
        }

        list_->setIssues(issues);
        details_->setIssues(issues);
        problems_->setIssues(issues, names);
        preview_->setText(doc_->generatedText());
        preview_->setFileLabel(doc_->filePath().isEmpty() ? tr("Not saved yet") : QFileInfo(doc_->filePath()).absoluteFilePath());

        int errors = 0, warnings = 0;
        for (const Issue& i : issues)
        {
            if (i.severity == Severity::Error)
                ++errors;
            else if (i.severity == Severity::Warning)
                ++warnings;
        }
        statusLabel_->setText(tr("%n input action(s), %1 mapping(s)", nullptr, map.actions.size()).arg(mappings));
        issueLabel_->setText(errors || warnings ? tr("%n error(s), %1 warning(s)", nullptr, errors).arg(warnings) : tr("No problems"));
        problemsDock_->setWindowTitle(errors || warnings ? tr("Problems (%1)").arg(errors + warnings) : tr("Problems"));
    }

    QString InputMapTab::filePath() const
    {
        return doc_->filePath();
    }

    QString InputMapTab::displayName() const
    {
        return doc_->filePath().isEmpty() ? tr("Untitled Input Map") : QFileInfo(doc_->filePath()).fileName();
    }

    bool InputMapTab::isModified() const
    {
        return doc_->isModified();
    }

    bool InputMapTab::openFile(const QString& path, QString* error)
    {
        return doc_->load(path, error);
    }

    bool InputMapTab::save()
    {
        return doc_->filePath().isEmpty() ? saveAs() : writeTo(doc_->filePath());
    }

    bool InputMapTab::saveAs()
    {
        const QString start = doc_->filePath().isEmpty() ? QDir(context()->projectDir()).absoluteFilePath(QStringLiteral("default.imap")) : doc_->filePath();
        const QString path = QFileDialog::getSaveFileName(this, tr("Save input map"), start, tr(kFilter));
        return !path.isEmpty() && writeTo(path);
    }

    bool InputMapTab::writeTo(const QString& path)
    {
        int errors = 0;
        for (const Issue& i : validate(doc_->map(), doc_->loadWarnings()))
            if (i.severity == Severity::Error) ++errors;
        if (errors > 0)
        {
            const auto r = QMessageBox::warning(this, tr("Problems found"),
                tr("This map has %n error(s). Parts of it may not work in the engine.\n\nSave anyway?", nullptr, errors),
                QMessageBox::Save | QMessageBox::Cancel, QMessageBox::Cancel);
            if (r != QMessageBox::Save) return false;
        }

        QString err;
        if (!doc_->save(path, &err))
        {
            QMessageBox::critical(this, tr("Could not save file"), tr("%1\n\n%2").arg(path, err));
            return false;
        }
        showStatus(tr("Saved %1").arg(QFileInfo(path).fileName()), 3000);
        emit titleChanged();
        emit fileSaved(doc_->filePath());
        return true;
    }
}
