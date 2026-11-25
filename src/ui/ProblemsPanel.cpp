#include "ui/ProblemsPanel.h"
#include "framework/EditorTab.h"
#include "ui/KesStyle.h"

#include <QHBoxLayout>
#include <QHeaderView>
#include <QScrollBar>
#include <QSettings>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <algorithm>

namespace
{
    constexpr int kTabRole = Qt::UserRole;
    constexpr int kIndexRole = Qt::UserRole + 1;

    int rank(ProblemSeverity s)
    {
        return s == ProblemSeverity::Error ? 0 : s == ProblemSeverity::Warning ? 1 : 2;
    }

    QString iconName(ProblemSeverity s)
    {
        return s == ProblemSeverity::Error ? QStringLiteral("error") : s == ProblemSeverity::Warning ? QStringLiteral("warning") : QStringLiteral("info");
    }
}

ProblemsPanel::ProblemsPanel(Shell shell, QWidget* parent)
    : QWidget(parent)
    , m_shell(std::move(shell))
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto* bar = new QHBoxLayout;
    bar->setContentsMargins(4, 2, 4, 2);
    m_currentOnly = new QToolButton(this);
    m_currentOnly->setText(tr("Current Tab Only"));
    m_currentOnly->setCheckable(true);
    m_currentOnly->setAutoRaise(true);
    m_currentOnly->setChecked(QSettings().value("problems/currentOnly", false).toBool());
    bar->addWidget(m_currentOnly);
    bar->addStretch(1);
    layout->addLayout(bar);

    m_tree = new QTreeWidget(this);
    m_tree->setColumnCount(2);
    m_tree->setHeaderLabels({ tr("Problem"), tr("Where") });
    m_tree->setUniformRowHeights(true);
    m_tree->setIndentation(14);
    m_tree->header()->setStretchLastSection(false);
    m_tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_tree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_tree->setToolTip(tr("Double-click to go to the problem"));
    layout->addWidget(m_tree, 1);

    connect(m_currentOnly, &QToolButton::toggled, this, [this](bool on)
        {
            QSettings().setValue("problems/currentOnly", on);
            scheduleRefresh();
        });
    connect(m_tree, &QTreeWidget::itemActivated, this, [this](QTreeWidgetItem* item, int) { activateItem(item); });
}

void ProblemsPanel::scheduleRefresh()
{
    if (m_refreshQueued)
        return;
    m_refreshQueued = true;
    QTimer::singleShot(0, this, [this]
        {
            m_refreshQueued = false;
            refresh();
        });
}

void ProblemsPanel::refresh()
{
    const QList<EditorTab*> all = m_shell.tabs();
    EditorTab* current = m_shell.current();
    QList<EditorTab*> shown;
    if (m_currentOnly->isChecked())
    {
        if (current)
            shown << current;
    }
    else
        shown = all;

    const int scroll = m_tree->verticalScrollBar() ? m_tree->verticalScrollBar()->value() : 0;
    m_tree->clear();

    int errors = 0, warnings = 0;
    for (EditorTab* tab : all)
    {
        errors += tab->problemCount(ProblemSeverity::Error);
        warnings += tab->problemCount(ProblemSeverity::Warning);
    }

    const bool grouped = shown.size() > 1 || !m_currentOnly->isChecked();
    for (EditorTab* tab : shown)
    {
        const ProblemList& problems = tab->problems();
        if (problems.isEmpty())
            continue;

        QTreeWidgetItem* parent = nullptr;
        if (grouped)
        {
            const int e = tab->problemCount(ProblemSeverity::Error);
            const int w = tab->problemCount(ProblemSeverity::Warning);
            QStringList counts;
            if (e) counts << tr("%n error(s)", nullptr, e);
            if (w) counts << tr("%n warning(s)", nullptr, w);
            parent = new QTreeWidgetItem(m_tree, { tab->displayName(), counts.join(QStringLiteral(", ")) });
            parent->setIcon(0, kes::style::icon(iconName(e ? ProblemSeverity::Error : w ? ProblemSeverity::Warning : ProblemSeverity::Info)));
            QFont f = parent->font(0);
            f.setBold(true);
            parent->setFont(0, f);
            parent->setData(0, kTabRole, QVariant::fromValue<QObject*>(tab));
            parent->setData(0, kIndexRole, -1);
        }

        QList<int> order;
        for (int i = 0; i < problems.size(); ++i)
            order << i;
        std::stable_sort(order.begin(), order.end(), [&](int a, int b) { return rank(problems[a].severity) < rank(problems[b].severity); });

        for (int i : order)
        {
            const Problem& p = problems[i];
            auto* item = parent ? new QTreeWidgetItem(parent, { p.message, p.where }) : new QTreeWidgetItem(m_tree, { p.message, p.where });
            item->setIcon(0, kes::style::icon(iconName(p.severity)));
            item->setToolTip(0, p.message);
            item->setData(0, kTabRole, QVariant::fromValue<QObject*>(tab));
            item->setData(0, kIndexRole, i);
        }
        if (parent)
            parent->setExpanded(true);
    }
    m_tree->setRootIsDecorated(grouped);
    if (m_tree->verticalScrollBar())
        m_tree->verticalScrollBar()->setValue(scroll);

    emit countChanged(errors, warnings);
}

void ProblemsPanel::activateItem(QTreeWidgetItem* item)
{
    if (!item)
        return;
    QPointer<EditorTab> tab = qobject_cast<EditorTab*>(item->data(0, kTabRole).value<QObject*>());
    if (!tab || !m_shell.tabs().contains(tab.data()))
        return;
    const int index = item->data(0, kIndexRole).toInt();
    m_shell.activate(tab);
    if (index < 0 || index >= tab->problems().size())
        return;
    // Copy: locating may republish the list.
    const std::function<void()> locate = tab->problems()[index].locate;
    if (locate)
        locate();
}
