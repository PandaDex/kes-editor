#pragma once
// Look of the KES Editor. tools/buildLauncher has an older copy of this file with the previous palette.
#include <QColor>
#include <QIcon>
#include <QString>

class QApplication;
class QPainter;
class QRectF;

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
// new open save undo redo refresh plus minus trash copy up down left right search capture
// keyboard mouse gamepad warning error info
// Lucide: group ungroup visgroup move scale rotate snap view3d
QIcon icon(const QString& name);
void paintIcon(QPainter& p, const QRectF& rect, const QString& name, const QColor& color);

} // namespace kes::style
