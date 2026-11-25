#pragma once
#include <QList>
#include <QString>
#include <functional>

enum class ProblemSeverity
{
    Error,
    Warning,
    Info
};

// One entry in Forge's shared Problems dock. A tab publishes its list with EditorTab::setProblems().
struct Problem
{
    ProblemSeverity severity = ProblemSeverity::Error;
    QString message;
    QString where;
    // Called after the tab is made current; leave empty when there is nothing to jump to.
    std::function<void()> locate;
};

using ProblemList = QList<Problem>;
