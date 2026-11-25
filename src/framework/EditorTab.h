#pragma once
#include "framework/Problem.h"
#include <QList>
#include <QMainWindow>

class EditorContext;
class QMenu;

// One document tab in Forge's main window. A tab is a QMainWindow of its own, so it keeps its
// docks, toolbars and status bar; its menus are shown in Forge's menu bar while it is current.
// Shortcuts of actions added to the tab or its menus only fire while the tab is current.
class EditorTab : public QMainWindow
{
    Q_OBJECT
public:
    EditorTab(EditorContext* ctx, const QString& moduleId, QWidget* parent = nullptr);

    EditorContext* context() const { return m_ctx; }
    const QString& moduleId() const { return m_moduleId; }

    // The asset this tab edits; empty while it has never been saved.
    virtual QString filePath() const = 0;
    virtual QString displayName() const;
    virtual bool isModified() const = 0;

    virtual bool openFile(const QString& path, QString* error) = 0;
    // Only for modules with one shared tab: start over with an empty document (after canClose()).
    virtual void newDocument() {}
    virtual bool save() = 0; // falls back to saveAs() without a path
    virtual bool saveAs() = 0;
    // Asks to save unsaved changes; false = the user cancelled.
    virtual bool canClose();
    virtual bool handlesFile(const QString& path) const;

    // Forge-wide events
    virtual void projectChanged() {}
    virtual void assetSaved(const QString& path) { Q_UNUSED(path); }
    virtual void activated() {}

    const QList<QMenu*>& menus() const { return m_menus; }

    // Shown in Forge's Problems dock.
    const ProblemList& problems() const { return m_problems; }
    void setProblems(const ProblemList& problems);
    int problemCount(ProblemSeverity severity) const;

    void restoreLayout();
    void saveLayout();

signals:
    void titleChanged();
    void fileSaved(const QString& path);
    void openRequested(const QString& path);
    void problemsChanged();
    void showProblemsRequested();

protected:
    QMenu* addTabMenu(const QString& title);
    void showStatus(const QString& text, int timeoutMs = 4000);

private:
    EditorContext* m_ctx;
    QString m_moduleId;
    QList<QMenu*> m_menus;
    ProblemList m_problems;
};
