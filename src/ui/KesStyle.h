#pragma once
// Look of the KES Editor. tools/buildLauncher has an older copy of this file with the previous palette.
#include <QColor>
#include <QIcon>
#include <QString>

class QApplication;

namespace kes::style {

void apply(QApplication& app);

// "<document>[*] - <tool>"
QString windowTitle(const QString& document, const QString& tool);

QColor accent();
QColor accentDim();
QColor warning();
QColor error();
QColor info();
QColor text();
QColor muted();
QColor disabled();
QColor base();
QColor window();
QColor panel();
QColor border();

// Painted 16px line icons, so no resource files are needed:
// new open save undo redo refresh plus trash copy up down left right search capture
// keyboard mouse gamepad warning error info
QIcon icon(const QString& name);

} // namespace kes::style
