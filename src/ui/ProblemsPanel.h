#pragma once
#include <QPointer>
#include <QWidget>
#include <functional>

class EditorTab;
class QToolButton;
class QTreeWidget;
class QTreeWidgetItem;

// Forge's shared Problems dock: the problems every open tab publishes, grouped by tab.
class ProblemsPanel : public QWidget
{
    Q_OBJECT
public:
    struct Shell
    {
        std::function<QList<EditorTab*>()> tabs;
        std::function<EditorTab*()> current;
        std::function<void(EditorTab*)> activate;
    };

    ProblemsPanel(Shell shell, QWidget* parent = nullptr);

    // Rebuilds on the next event loop turn, so bursts of changes cost one rebuild.
    void scheduleRefresh();

signals:
    void countChanged(int errors, int warnings);

private:
    void refresh();
    void activateItem(QTreeWidgetItem* item);

    Shell m_shell;
    QTreeWidget* m_tree = nullptr;
    QToolButton* m_currentOnly = nullptr;
    bool m_refreshQueued = false;
};
