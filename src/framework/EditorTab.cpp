#include "framework/EditorTab.h"
#include <QFileInfo>
#include <QMenu>
#include <QMessageBox>
#include <QSettings>
#include <QStatusBar>

EditorTab::EditorTab(EditorContext* ctx, const QString& moduleId, QWidget* parent)
    : QMainWindow(parent, Qt::Widget)
    , m_ctx(ctx)
    , m_moduleId(moduleId)
{
    setDockNestingEnabled(true);
}

QString EditorTab::displayName() const
{
    const QString path = filePath();
    return path.isEmpty() ? tr("Untitled") : QFileInfo(path).fileName();
}

bool EditorTab::canClose()
{
    if (!isModified()) return true;
    const auto answer = QMessageBox::warning(this, tr("Unsaved Changes"), tr("Save changes to %1?").arg(displayName()),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
    if (answer == QMessageBox::Save) return save();
    return answer == QMessageBox::Discard;
}

bool EditorTab::handlesFile(const QString& path) const
{
    const QString mine = filePath();
    return !mine.isEmpty() && QFileInfo(mine).absoluteFilePath().compare(QFileInfo(path).absoluteFilePath(), Qt::CaseInsensitive) == 0;
}

void EditorTab::restoreLayout()
{
    const QByteArray state = QSettings().value("tabs/" + m_moduleId + "/state").toByteArray();
    if (!state.isEmpty()) restoreState(state);
}

void EditorTab::saveLayout()
{
    QSettings().setValue("tabs/" + m_moduleId + "/state", saveState());
}

QMenu* EditorTab::addTabMenu(const QString& title)
{
    auto* menu = new QMenu(title, this);
    m_menus << menu;
    return menu;
}

void EditorTab::showStatus(const QString& text, int timeoutMs)
{
    statusBar()->showMessage(text, timeoutMs);
}
