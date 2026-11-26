#include "ui/KesStyle.h"

#include <QApplication>
#include <QHash>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QPixmap>
#include <QPolygonF>
#include <QRegularExpression>
#include <QtMath>
#include <cmath>
#include <QStyleFactory>

namespace kes::style {

namespace {

// Colours follow the KES website (cool blue greys, one amber accent), lifted off black so surfaces stay distinct.
const QHash<QString, QString>& tokens()
{
    static const QHash<QString, QString> t = {
        { "bg0", "#07080b" },      // separators, deepest frame
        { "window", "#1a1d24" },   // window, menu bar, toolbars, dock titles
        { "base", "#111318" },     // inputs, lists, text views
        { "alt", "#16191f" },      // alternate rows
        { "panel", "#20242c" },    // section headers, raised panels, cards
        { "raised", "#282d37" },   // buttons, menus, popups
        { "hover", "#323844" },
        { "pressed", "#1c2027" },
        { "line", "#2d323d" },     // dividers
        { "border", "#3b4250" },   // control outlines
        { "borderHover", "#566072" },
        { "text", "#e3e6ec" },
        { "bright", "#f4f5f7" },
        { "muted", "#adb3bf" },
        { "dim", "#9197a5" },
        { "disabled", "#5f6575" },
        { "accent", "#f5b54a" },
        { "accentStrong", "#8a6524" }, // text selection
        { "accentDim", "#4d3c1d" },    // selected rows
        { "accentBg", "#3b301b" },     // checked tool buttons
        { "accentText", "#ffd99a" },
        { "warning", "#f2c94c" },
        { "error", "#f26d6d" },
        { "info", "#6aa8ff" },
    };
    return t;
}

QColor tok(const char* name) { return QColor(tokens().value(QLatin1String(name))); }

const char* kStyleSheet = R"(
* { outline: 0; }
QWidget { font-size: 9pt; }
QToolTip { background: @raised; color: @bright; border: 1px solid @borderHover; padding: 5px 8px; border-radius: 6px; }

QMainWindow::separator { background: @bg0; width: 3px; height: 3px; }
QMainWindow::separator:hover { background: @accent; }
QDockWidget { titlebar-close-icon: none; titlebar-normal-icon: none; font-weight: 600; color: @muted; }
QDockWidget::title { background: @window; padding: 7px 10px; border-bottom: 1px solid @line; text-align: left; }
QDialog { background: @window; }

QTabWidget::pane { border: none; border-top: 1px solid @line; top: -1px; }
QTabBar { qproperty-drawBase: 0; }
QTabBar::tab { background: transparent; color: @dim; padding: 7px 14px; border: none; border-bottom: 2px solid transparent; margin-right: 2px; }
QTabBar::tab:selected { color: @bright; border-bottom: 2px solid @accent; }
QTabBar::tab:hover:!selected { color: @text; border-bottom: 2px solid @borderHover; }

QMenuBar { background: @window; padding: 3px 4px; border-bottom: 1px solid @line; }
QMenuBar::item { padding: 4px 10px; border-radius: 6px; color: @text; }
QMenuBar::item:selected { background: @hover; color: @bright; }
QMenu { background: @raised; border: 1px solid @border; border-radius: 8px; padding: 5px; }
QMenu::item { padding: 6px 28px 6px 22px; border-radius: 5px; color: @text; }
QMenu::item:selected { background: @hover; color: @bright; }
QMenu::item:disabled { color: @disabled; }
QMenu::separator { height: 1px; background: @line; margin: 5px 8px; }

QToolBar { background: @window; border: none; border-bottom: 1px solid @line; spacing: 4px; padding: 4px 6px; }
QToolBar::separator { background: @line; width: 1px; height: 1px; margin: 4px 6px; }
QToolBar#toolStrip { border-bottom: none; border-right: 1px solid @line; padding: 6px 4px; spacing: 6px; }
QToolButton { border: 1px solid transparent; border-radius: 6px; padding: 4px 6px; color: @text; }
QToolButton:hover { background: @hover; border-color: @border; }
QToolButton:pressed { background: @pressed; }
QToolButton:checked { background: @accentBg; border-color: @accent; color: @accentText; }
QToolButton:disabled { color: @disabled; }
QToolButton::menu-indicator { image: none; width: 0px; }
QToolButton[listening="true"] { background: @accentBg; border-color: @accent; }

QPushButton { background: @raised; border: 1px solid @border; border-radius: 6px; padding: 5px 14px; color: @text; }
QPushButton:hover { background: @hover; border-color: @borderHover; color: @bright; }
QPushButton:pressed { background: @pressed; }
QPushButton:default, QPushButton#Primary { background: @bright; border-color: @bright; color: @window; font-weight: 600; }
QPushButton:default:hover, QPushButton#Primary:hover { background: #ffffff; border-color: #ffffff; }
QPushButton:default:pressed, QPushButton#Primary:pressed { background: #d9dce2; }
QPushButton:disabled { color: @disabled; border-color: @line; background: @panel; }
QPushButton#KeyButton { background: @base; border: 1px solid @border; border-radius: 6px; text-align: left; padding: 3px 24px 3px 8px; font-weight: normal; color: @text; }
QPushButton#KeyButton:hover { border-color: @borderHover; }
QPushButton#KeyButton[invalid="true"] { color: @error; border-color: #6a2f33; }

QLineEdit, QSpinBox, QDoubleSpinBox, QComboBox, QPlainTextEdit, QTextEdit {
    background: @base; border: 1px solid @border; border-radius: 6px; padding: 3px 6px; color: @text;
    selection-background-color: @accentStrong; selection-color: @bright; }
QLineEdit:hover, QSpinBox:hover, QDoubleSpinBox:hover, QComboBox:hover { border-color: @borderHover; }
QLineEdit:focus, QSpinBox:focus, QDoubleSpinBox:focus, QComboBox:focus, QPlainTextEdit:focus, QTextEdit:focus { border-color: @accent; }
QLineEdit:disabled, QSpinBox:disabled, QDoubleSpinBox:disabled, QComboBox:disabled { color: @disabled; background: @window; border-color: @line; }
QComboBox::drop-down { border: none; width: 18px; }
QComboBox QAbstractItemView { background: @raised; border: 1px solid @border; selection-background-color: @hover; selection-color: @bright; padding: 3px; }

QCheckBox, QRadioButton { spacing: 6px; }
QCheckBox::indicator, QRadioButton::indicator { width: 13px; height: 13px; border: 1px solid @borderHover; background: @base; }
QCheckBox::indicator { border-radius: 4px; }
QRadioButton::indicator { border-radius: 7px; }
QCheckBox::indicator:hover, QRadioButton::indicator:hover { border-color: @accent; }
QCheckBox::indicator:checked, QRadioButton::indicator:checked { background: @accent; border-color: @accent; }
QCheckBox::indicator:disabled, QRadioButton::indicator:disabled { border-color: @line; background: @window; }

QHeaderView::section { background: @window; color: @muted; border: none; border-right: 1px solid @line; border-bottom: 1px solid @line; padding: 5px 6px; }
QTreeView, QListView, QTreeWidget, QListWidget, QTableWidget, QTableView {
    background: @base; alternate-background-color: @alt; border: 1px solid @line; selection-background-color: @accentDim; selection-color: @bright; }
QTreeView::item, QListView::item { padding: 3px 2px; }
QTreeView::item:hover:!selected, QListView::item:hover:!selected { background: @panel; }
QTreeView::item:selected, QListView::item:selected { background: @accentDim; color: @bright; }

QScrollArea { border: none; background: transparent; }
QScrollBar:vertical { background: transparent; width: 10px; margin: 0; }
QScrollBar:horizontal { background: transparent; height: 10px; margin: 0; }
QScrollBar::handle:vertical { background: @border; min-height: 24px; border-radius: 3px; margin: 2px; }
QScrollBar::handle:horizontal { background: @border; min-width: 24px; border-radius: 3px; margin: 2px; }
QScrollBar::handle:hover { background: @borderHover; }
QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; }
QScrollBar::add-page, QScrollBar::sub-page { background: none; }

QSlider::groove:horizontal { height: 4px; background: @border; border-radius: 2px; }
QSlider::sub-page:horizontal { background: @accent; border-radius: 2px; }
QSlider::handle:horizontal { background: @bright; width: 12px; height: 12px; margin: -4px 0; border-radius: 6px; }
QProgressBar { background: @base; border: 1px solid @border; border-radius: 6px; text-align: center; color: @text; }
QProgressBar::chunk { background: @accent; border-radius: 5px; }
QGroupBox { border: 1px solid @line; border-radius: 8px; margin-top: 14px; padding-top: 6px; }
QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 4px; color: @muted; }

QStatusBar { background: @window; color: @muted; border-top: 1px solid @line; }
QStatusBar::item { border: none; }
QStatusBar QLabel { padding: 0 8px; border-left: 1px solid @line; color: @muted; }
QSplitter::handle { background: @bg0; }
QSplitter::handle:hover { background: @accent; }

QLabel#Muted, QLabel#hint { color: @muted; }
QLabel#Heading { color: @muted; font-weight: 600; margin-top: 8px; }
QLabel#Title { font-size: 14pt; font-weight: 700; color: @bright; }
QLabel#EmptyState { color: @dim; font-size: 11pt; }
QLabel#Warning { color: @warning; }
QLabel#Error { color: @error; }

QFrame#Section { background: @panel; border: 1px solid @line; border-radius: 8px; }
QFrame#SectionHeader { background: @raised; border: none; border-top-left-radius: 8px; border-top-right-radius: 8px; }
QLabel#SectionTitle { font-weight: 600; color: @bright; }
QFrame#Card { background: @panel; border: 1px solid @line; border-radius: 8px; }
QFrame#Card[expanded="true"] { border-color: @borderHover; }
QFrame#CardHeader { background: transparent; border: none; }
QFrame#ModifierRow { background: @base; border: 1px solid @line; border-radius: 6px; }
QFrame#Divider { background: @line; max-height: 1px; min-height: 1px; border: none; }
QFrame#Popup { background: @raised; border: 1px solid @border; border-radius: 8px; }

QWidget#AssetTopBar { background: @window; border-bottom: 1px solid @line; }
QWidget#AssetBottomBar { background: @window; border-top: 1px solid @line; }
QToolButton#AddButton { background: @accent; border: 1px solid @accent; color: @window; font-weight: 700; padding: 4px 12px; }
QToolButton#AddButton:hover { background: #f7c46e; border-color: #f7c46e; }
QToolButton#AddButton:pressed { background: #d99c38; }
QWidget#PathBar { background: @base; border: 1px solid @border; border-radius: 6px; min-height: 24px; }
QWidget#PathBar:hover { border-color: @borderHover; }
QToolButton#Crumb { border: none; border-radius: 4px; padding: 2px 5px; color: @muted; }
QToolButton#Crumb:hover { background: @hover; color: @bright; }
QToolButton#Crumb[current="true"] { color: @bright; font-weight: 600; }
QToolButton#CrumbChevron { border: none; border-radius: 4px; padding: 2px 3px; color: @dim; font-size: 11pt; }
QToolButton#CrumbChevron:hover { background: @hover; color: @accent; }
QLineEdit#PathEdit { border: none; background: transparent; padding: 1px 4px; }
QLineEdit#PathEdit[invalid="true"] { color: @error; }
QListView#AssetGrid { border: none; background: @base; border-radius: 8px; }
QTreeView#AssetTree { border: none; background: @window; }
QTreeView#AssetTree::item { padding: 3px 2px; border-radius: 4px; }
)";

// Lucide icons (https://lucide.dev, ISC), copied verbatim on their 24 unit grid.
// "rect x y w h rx" and "circle cx cy r" stand in for the matching SVG elements.
const QHash<QString, QStringList>& lucideIcons()
{
    static const QHash<QString, QStringList> icons = {
        { "group", { "M3 7V5c0-1.1.9-2 2-2h2", "M17 3h2c1.1 0 2 .9 2 2v2", "M21 17v2c0 1.1-.9 2-2 2h-2",
                     "M7 21H5c-1.1 0-2-.9-2-2v-2", "rect 7 7 7 5 1", "rect 10 12 7 5 1" } },
        { "ungroup", { "rect 11 14 10 7 2", "rect 3 3 10 7 2" } },
        { "visgroup", { "M12.83 2.18a2 2 0 0 0-1.66 0L2.6 6.08a1 1 0 0 0 0 1.83l8.58 3.91a2 2 0 0 0 .83.18 2 2 0 0 0 .83-.18l8.58-3.9a1 1 0 0 0 0-1.831z",
                        "M16 17h6", "M19 14v6", "M2 12a1 1 0 0 0 .58.91l8.6 3.91a2 2 0 0 0 .825.178",
                        "M2 17a1 1 0 0 0 .58.91l8.6 3.91a2 2 0 0 0 1.65 0l2.116-.962" } },
        { "move", { "M5 3v16h16", "m5 19 6-6", "m2 6 3-3 3 3", "m18 16 3 3-3 3" } },
        { "scale", { "M5 7v11a1 1 0 0 0 1 1h11", "M5.293 18.707 11 13", "circle 19 19 2", "circle 5 5 2" } },
        { "snap", { "m12 15 4 4", "m5 8 4 4",
                    "M2.352 10.648a1.205 1.205 0 0 0 0 1.704l2.296 2.296a1.205 1.205 0 0 0 1.704 0l6.029-6.029a1 1 0 1 1 3 3l-6.029 6.029a1.205 1.205 0 0 0 0 1.704l2.296 2.296a1.205 1.205 0 0 0 1.704 0l6.365-6.367A1 1 0 0 0 8.716 4.282z" } },
        { "view3d", { "M21 8a2 2 0 0 0-1-1.73l-7-4a2 2 0 0 0-2 0l-7 4A2 2 0 0 0 3 8v8a2 2 0 0 0 1 1.73l7 4a2 2 0 0 0 2 0l7-4A2 2 0 0 0 21 16Z",
                      "m3.3 7 8.7 5 8.7-5", "M12 22V12" } },
        { "rotate", { "m15.194 13.707 3.814 1.86-1.86 3.814", "M16.47214 7.52786 A 5 10 0 1 0 13 21.79796",
                      "M21.79796 11 A 10 5 0 1 0 19 15.57071" } },
    };
    return icons;
}

void arcTo(QPainterPath& path, QPointF from, qreal rx, qreal ry, qreal phiDeg, bool large, bool sweep, QPointF to)
{
    rx = std::abs(rx);
    ry = std::abs(ry);
    if (rx < 1e-9 || ry < 1e-9 || from == to)
    {
        path.lineTo(to);
        return;
    }
    // Endpoint to centre parameterisation, SVG spec appendix B.2.4.
    const qreal phi = qDegreesToRadians(phiDeg), cs = std::cos(phi), sn = std::sin(phi);
    const qreal dx = (from.x() - to.x()) / 2, dy = (from.y() - to.y()) / 2;
    const qreal x1 = cs * dx + sn * dy, y1 = -sn * dx + cs * dy;
    const qreal lambda = x1 * x1 / (rx * rx) + y1 * y1 / (ry * ry);
    if (lambda > 1)
    {
        rx *= std::sqrt(lambda);
        ry *= std::sqrt(lambda);
    }
    const qreal num = rx * rx * ry * ry - rx * rx * y1 * y1 - ry * ry * x1 * x1;
    const qreal den = rx * rx * y1 * y1 + ry * ry * x1 * x1;
    const qreal coef = (large == sweep ? -1 : 1) * std::sqrt(std::max<qreal>(0, num / den));
    const qreal cxp = coef * rx * y1 / ry, cyp = -coef * ry * x1 / rx;
    const qreal cx = cs * cxp - sn * cyp + (from.x() + to.x()) / 2;
    const qreal cy = sn * cxp + cs * cyp + (from.y() + to.y()) / 2;
    const qreal t1 = std::atan2((y1 - cyp) / ry, (x1 - cxp) / rx);
    qreal dt = std::atan2((-y1 - cyp) / ry, (-x1 - cxp) / rx) - t1;
    if (!sweep && dt > 0) dt -= 2 * M_PI;
    if (sweep && dt < 0) dt += 2 * M_PI;
    const int steps = std::max(4, int(std::ceil(std::abs(dt) / (M_PI / 16))));
    for (int i = 1; i <= steps; ++i)
    {
        const qreal t = t1 + dt * i / steps;
        const qreal ux = rx * std::cos(t), uy = ry * std::sin(t);
        path.lineTo(i == steps ? to : QPointF(cs * ux - sn * uy + cx, sn * ux + cs * uy + cy));
    }
}

QPainterPath svgPath(const QString& d)
{
    static const QRegularExpression tokenRe(QStringLiteral("[A-Za-z]|[-+]?(?:\\d+\\.?\\d*|\\.\\d+)(?:[eE][-+]?\\d+)?"));
    QStringList tokens;
    for (auto it = tokenRe.globalMatch(d); it.hasNext();) tokens << it.next().captured(0);

    QPainterPath path;
    QPointF cur, start, lastCtrl;
    QChar cmd, prevCmd;
    int i = 0;
    auto num = [&] { return i < tokens.size() ? tokens[i++].toDouble() : 0.0; };
    while (i < tokens.size())
    {
        if (tokens[i][0].isLetter()) cmd = tokens[i++][0];
        else if (cmd == 'M') cmd = 'L';
        else if (cmd == 'm') cmd = 'l';
        const bool rel = cmd.isLower();
        const QPointF base = rel ? cur : QPointF();
        auto pt = [&] { const qreal x = num(); const qreal y = num(); return base + QPointF(x, y); };
        switch (cmd.toUpper().unicode())
        {
        case 'M': cur = start = pt(); path.moveTo(cur); break;
        case 'L': cur = pt(); path.lineTo(cur); break;
        case 'H': cur.setX((rel ? cur.x() : 0) + num()); path.lineTo(cur); break;
        case 'V': cur.setY((rel ? cur.y() : 0) + num()); path.lineTo(cur); break;
        case 'C':
        {
            const QPointF c1 = pt(), c2 = pt(), e = pt();
            path.cubicTo(c1, c2, e);
            lastCtrl = c2;
            cur = e;
            break;
        }
        case 'S':
        {
            const bool smooth = QStringLiteral("CcSs").contains(prevCmd);
            const QPointF c1 = smooth ? cur * 2 - lastCtrl : cur;
            const QPointF c2 = pt(), e = pt();
            path.cubicTo(c1, c2, e);
            lastCtrl = c2;
            cur = e;
            break;
        }
        case 'Q':
        {
            const QPointF c = pt(), e = pt();
            path.quadTo(c, e);
            cur = e;
            break;
        }
        case 'A':
        {
            const qreal rx = num(), ry = num(), rot = num();
            const bool large = num() != 0, sweep = num() != 0;
            const QPointF e = pt();
            arcTo(path, cur, rx, ry, rot, large, sweep, e);
            cur = e;
            break;
        }
        case 'Z': path.closeSubpath(); cur = start; break;
        default: ++i; break;
        }
        prevCmd = cmd;
    }
    return path;
}

bool drawLucide(QPainter& p, const QString& n)
{
    const auto it = lucideIcons().constFind(n);
    if (it == lucideIcons().constEnd()) return false;
    p.save();
    p.scale(16.0 / 24.0, 16.0 / 24.0);
    QPen pen = p.pen();
    pen.setWidthF(pen.widthF() * 24.0 / 16.0);
    p.setPen(pen);
    for (const QString& part : *it)
    {
        const QStringList f = part.split(' ');
        if (f[0] == "rect")
            p.drawRoundedRect(QRectF(f[1].toDouble(), f[2].toDouble(), f[3].toDouble(), f[4].toDouble()), f[5].toDouble(), f[5].toDouble());
        else if (f[0] == "circle")
            p.drawEllipse(QPointF(f[1].toDouble(), f[2].toDouble()), f[3].toDouble(), f[3].toDouble());
        else
            p.drawPath(svgPath(part));
    }
    p.restore();
    return true;
}

void drawGlyph(QPainter& p, const QString& n, const QColor& c)
{
    if (drawLucide(p, n)) return;
    auto line = [&](qreal x1, qreal y1, qreal x2, qreal y2) { p.drawLine(QPointF(x1, y1), QPointF(x2, y2)); };
    QColor ink = c;
    auto dot = [&](qreal x, qreal y, qreal r = 0.9) {
        p.save(); p.setPen(Qt::NoPen); p.setBrush(ink); p.drawEllipse(QPointF(x, y), r, r); p.restore();
    };
    auto tint = [&](const QColor& col) { ink = col; QPen pen = p.pen(); pen.setColor(col); p.setPen(pen); };

    if (n == "plus")        { line(8, 3.5, 8, 12.5); line(3.5, 8, 12.5, 8); }
    else if (n == "minus")  line(3.5, 8, 12.5, 8);
    else if (n == "trash")
    {
        line(3, 4.5, 13, 4.5);
        p.drawPolyline(QPolygonF({ {6.5, 4.5}, {6.5, 3}, {9.5, 3}, {9.5, 4.5} }));
        p.drawPolygon(QPolygonF({ {4.5, 4.5}, {5.3, 13}, {10.7, 13}, {11.5, 4.5} }));
        line(7.2, 7, 7.2, 11); line(8.8, 7, 8.8, 11);
    }
    else if (n == "up")     p.drawPolyline(QPolygonF({ {4, 10}, {8, 6}, {12, 10} }));
    else if (n == "down")   p.drawPolyline(QPolygonF({ {4, 6}, {8, 10}, {12, 6} }));
    else if (n == "right")  p.drawPolyline(QPolygonF({ {6, 4}, {10, 8}, {6, 12} }));
    else if (n == "left")   p.drawPolyline(QPolygonF({ {10, 4}, {6, 8}, {10, 12} }));
    else if (n == "refresh")
    {
        p.drawArc(QRectF(3, 3, 10, 10), 60 * 16, 290 * 16);
        p.drawPolyline(QPolygonF({ {10.2, 1.8}, {10.8, 4.6}, {8, 5.2} }));
    }
    else if (n == "copy")
    {
        p.drawRoundedRect(QRectF(5.5, 5.5, 7, 8), 1, 1);
        p.drawPolyline(QPolygonF({ {3.5, 10.5}, {3.5, 2.5}, {9.5, 2.5} }));
    }
    else if (n == "keyboard")
    {
        p.drawRoundedRect(QRectF(1.5, 4, 13, 8), 1.5, 1.5);
        p.save(); QPen pen = p.pen(); pen.setWidthF(1.7); p.setPen(pen);
        for (qreal x : { 4.2, 6.7, 9.2, 11.7 }) p.drawPoint(QPointF(x, 6.8));
        p.drawPoint(QPointF(4.2, 9.1)); p.drawPoint(QPointF(11.7, 9.1));
        p.restore();
        line(6.2, 9.1, 9.8, 9.1);
    }
    else if (n == "mouse")
    {
        p.drawRoundedRect(QRectF(4.5, 2, 7, 12), 3.5, 3.5);
        line(8, 2, 8, 6.5); line(4.5, 6.5, 11.5, 6.5);
    }
    else if (n == "gamepad")
    {
        p.drawRoundedRect(QRectF(1.5, 4.5, 13, 7), 3.5, 3.5);
        line(4.2, 8, 7, 8); line(5.6, 6.6, 5.6, 9.4);
        dot(10, 7.2, 1); dot(11.9, 8.9, 1);
    }
    else if (n == "capture") { p.drawEllipse(QPointF(8, 8), 5, 5); dot(8, 8, 2); }
    else if (n == "search")  { p.drawEllipse(QPointF(7, 7), 3.8, 3.8); line(9.9, 9.9, 13.5, 13.5); }
    else if (n == "new")
    {
        p.drawPolygon(QPolygonF({ {4, 2}, {9.5, 2}, {12, 4.5}, {12, 14}, {4, 14} }));
        p.drawPolyline(QPolygonF({ {9.5, 2}, {9.5, 4.5}, {12, 4.5} }));
    }
    else if (n == "open")
    {
        p.drawPolyline(QPolygonF({ {2, 13}, {2, 4}, {6, 4}, {7.5, 5.5}, {12.5, 5.5}, {12.5, 7} }));
        p.drawPolygon(QPolygonF({ {2, 13}, {4, 7}, {14.5, 7}, {12.5, 13} }));
    }
    else if (n == "save")
    {
        p.drawRoundedRect(QRectF(2.5, 2.5, 11, 11), 1.2, 1.2);
        p.drawRect(QRectF(5, 2.5, 6, 3.5));
        p.drawRect(QRectF(4.5, 8.5, 7, 5));
    }
    else if (n == "undo" || n == "redo")
    {
        if (n == "redo") { p.translate(16, 0); p.scale(-1, 1); }
        p.drawPolyline(QPolygonF({ {6, 3.5}, {3.5, 6}, {6, 8.5} }));
        QPainterPath path;
        path.moveTo(3.5, 6);
        path.cubicTo(12, 5, 13.5, 10, 9, 12);
        p.drawPath(path);
    }
    else if (n == "warning")
    {
        tint(warning());
        p.drawPolygon(QPolygonF({ {8, 2.5}, {14, 13}, {2, 13} }));
        line(8, 6.5, 8, 9.5);
        dot(8, 11.2);
    }
    else if (n == "error")
    {
        tint(error());
        p.drawEllipse(QPointF(8, 8), 6, 6);
        line(5.8, 5.8, 10.2, 10.2); line(10.2, 5.8, 5.8, 10.2);
    }
    else if (n == "info")
    {
        tint(info());
        p.drawEllipse(QPointF(8, 8), 6, 6);
        line(8, 7.3, 8, 11);
        dot(8, 5.2);
    }
}

QPixmap renderIcon(const QString& name, const QColor& color)
{
    constexpr qreal dpr = 2.0;
    QPixmap pm(QSize(16, 16) * dpr);
    pm.setDevicePixelRatio(dpr);
    pm.fill(Qt::transparent);

    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(color, 1.4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(Qt::NoBrush);
    drawGlyph(p, name, color);
    return pm;
}

} // namespace

QColor accent()    { return tok("accent"); }
QColor accentDim() { return tok("accentDim"); }
QColor warning()   { return tok("warning"); }
QColor error()     { return tok("error"); }
QColor info()      { return tok("info"); }
QColor text()      { return tok("text"); }
QColor muted()     { return tok("muted"); }
QColor disabled()  { return tok("disabled"); }
QColor base()      { return tok("base"); }
QColor window()    { return tok("window"); }
QColor panel()     { return tok("panel"); }
QColor border()    { return tok("border"); }

QString windowTitle(const QString& document, const QString& tool)
{
    return QStringLiteral("%1[*] - %2").arg(document, tool);
}

void apply(QApplication& app)
{
    app.setStyle(QStyleFactory::create(QStringLiteral("Fusion")));

    QPalette p;
    p.setColor(QPalette::Window, window());
    p.setColor(QPalette::WindowText, text());
    p.setColor(QPalette::Base, base());
    p.setColor(QPalette::AlternateBase, tok("alt"));
    p.setColor(QPalette::ToolTipBase, tok("raised"));
    p.setColor(QPalette::ToolTipText, tok("bright"));
    p.setColor(QPalette::Text, text());
    p.setColor(QPalette::Button, tok("raised"));
    p.setColor(QPalette::ButtonText, text());
    p.setColor(QPalette::BrightText, Qt::white);
    p.setColor(QPalette::Link, accent());
    p.setColor(QPalette::Highlight, tok("accentStrong"));
    p.setColor(QPalette::HighlightedText, tok("bright"));
    p.setColor(QPalette::PlaceholderText, tok("dim"));
    p.setColor(QPalette::Light, tok("borderHover"));
    p.setColor(QPalette::Midlight, tok("line"));
    p.setColor(QPalette::Mid, tok("raised"));
    p.setColor(QPalette::Dark, tok("bg0"));
    p.setColor(QPalette::Shadow, Qt::black);
    for (auto role : { QPalette::WindowText, QPalette::Text, QPalette::ButtonText })
        p.setColor(QPalette::Disabled, role, disabled());
    app.setPalette(p);

    // Substitute @token names with their colours.
    QString sheet = QString::fromLatin1(kStyleSheet);
    static const QRegularExpression re(QStringLiteral("@([A-Za-z0-9]+)"));
    QString out;
    out.reserve(sheet.size());
    qsizetype last = 0;
    for (auto it = re.globalMatch(sheet); it.hasNext();)
    {
        const auto m = it.next();
        out += QStringView(sheet).mid(last, m.capturedStart() - last);
        out += tokens().value(m.captured(1), m.captured(0));
        last = m.capturedEnd();
    }
    out += QStringView(sheet).mid(last);
    app.setStyleSheet(out);
}

QIcon icon(const QString& name)
{
    static QHash<QString, QIcon> cache;
    auto it = cache.constFind(name);
    if (it != cache.constEnd()) return *it;

    QIcon result;
    result.addPixmap(renderIcon(name, tok("text")), QIcon::Normal);
    result.addPixmap(renderIcon(name, tok("disabled")), QIcon::Disabled);
    cache.insert(name, result);
    return result;
}

void paintIcon(QPainter& p, const QRectF& rect, const QString& name, const QColor& color)
{
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    p.translate(rect.topLeft());
    p.scale(rect.width() / 16.0, rect.height() / 16.0);
    p.setPen(QPen(color, 1.4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(Qt::NoBrush);
    drawGlyph(p, name, color);
    p.restore();
}

} // namespace kes::style
