#include "ui/AssetPathBar.h"
#include "ui/FolderColors.h"

#include <QDir>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMenu>
#include <QMouseEvent>
#include <QStyle>
#include <QToolButton>

namespace
{
    bool inside(const QString& root, const QString& path)
    {
        const QString rel = QDir(root).relativeFilePath(path);
        return !rel.startsWith(QStringLiteral("..")) && !QDir::isAbsolutePath(rel);
    }
}

AssetPathBar::AssetPathBar(FolderColors* colors, QWidget* parent) : QWidget(parent), m_colors(colors)
{
    setObjectName("PathBar");
    setAttribute(Qt::WA_StyledBackground);
    setAttribute(Qt::WA_Hover);
    setCursor(Qt::IBeamCursor);
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(3, 1, 3, 1);
    layout->setSpacing(0);

    m_crumbs = new QWidget(this);
    m_crumbs->setCursor(Qt::ArrowCursor);
    m_crumbLayout = new QHBoxLayout(m_crumbs);
    m_crumbLayout->setContentsMargins(0, 0, 0, 0);
    m_crumbLayout->setSpacing(0);
    layout->addWidget(m_crumbs);
    layout->addStretch(1);

    m_edit = new QLineEdit(this);
    m_edit->setObjectName("PathEdit");
    m_edit->hide();
    m_edit->installEventFilter(this);
    layout->addWidget(m_edit, 1);
    connect(m_edit, &QLineEdit::returnPressed, this, [this]
        { finishEditing(true); });
    connect(m_edit, &QLineEdit::textEdited, this, [this]
        {
        m_edit->setProperty("invalid", false);
        m_edit->style()->unpolish(m_edit);
        m_edit->style()->polish(m_edit); });

    m_overflow = new QToolButton(m_crumbs);
    m_overflow->setObjectName("Crumb");
    m_overflow->setText(QStringLiteral("…"));
    m_overflow->setPopupMode(QToolButton::InstantPopup);
    m_overflow->setMenu(new QMenu(m_overflow));
    m_overflow->hide();

    connect(m_colors, &FolderColors::changed, this, [this]
        { rebuild(); });
}

void AssetPathBar::setPath(const QString& root, const QString& rootLabel, const QString& current)
{
    m_root = QDir::cleanPath(root);
    m_rootLabel = rootLabel;
    m_current = QDir::cleanPath(current);
    m_folders.clear();
    if (!m_root.isEmpty() && inside(m_root, m_current))
    {
        QString path = m_current;
        while (true)
        {
            m_folders.prepend(path);
            if (QDir(path) == QDir(m_root))
                break;
            const QString parent = QFileInfo(path).absolutePath();
            if (parent == path)
                break;
            path = parent;
        }
    }
    else if (!m_current.isEmpty())
    {
        m_folders << m_current;
    }
    if (m_edit->isVisible())
        finishEditing(false);
    rebuild();
}

QToolButton* AssetPathBar::chevron(const QString& folder)
{
    auto* button = new QToolButton(m_crumbs);
    button->setObjectName("CrumbChevron");
    button->setText(QStringLiteral("›"));
    button->setPopupMode(QToolButton::InstantPopup);
    auto* menu = new QMenu(button);
    button->setMenu(menu);
    connect(menu, &QMenu::aboutToShow, this, [this, menu, folder]
        {
        menu->clear();
        const QFileInfoList dirs = QDir(folder).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name | QDir::IgnoreCase);
        for (const QFileInfo& dir : dirs)
        {
            const QString path = dir.absoluteFilePath();
            QAction* a = menu->addAction(FolderColors::icon(m_colors->color(path), 16), dir.fileName(), this, [this, path]
                { emit navigate(path); });
            if (!m_current.isEmpty() && (QDir(path) == QDir(m_current) || inside(path, m_current)))
            {
                QFont f = a->font();
                f.setBold(true);
                a->setFont(f);
            }
        }
        if (dirs.isEmpty())
            menu->addAction(tr("No subfolders"))->setEnabled(false); });
    return button;
}

void AssetPathBar::rebuild()
{
    for (const QList<QWidget*>& segment : m_segments)
        for (QWidget* w : segment)
        {
            w->hide();
            w->deleteLater();
        }
    m_segments.clear();
    while (m_crumbLayout->count() > 0)
        delete m_crumbLayout->takeAt(0);

    m_crumbLayout->addWidget(m_overflow);
    for (int i = 0; i < m_folders.size(); ++i)
    {
        const QString folder = m_folders[i];
        auto* name = new QToolButton(m_crumbs);
        name->setObjectName("Crumb");
        name->setText(i == 0 && !m_rootLabel.isEmpty() ? m_rootLabel : QFileInfo(folder).fileName());
        name->setToolTip(QDir::toNativeSeparators(folder));
        name->setProperty("current", i == m_folders.size() - 1);
        const QColor color = m_colors->color(folder);
        if (color.isValid())
        {
            name->setIcon(FolderColors::icon(color, 14));
            name->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        }
        connect(name, &QToolButton::clicked, this, [this, folder]
            { emit navigate(folder); });
        QToolButton* more = chevron(folder);
        m_crumbLayout->addWidget(name);
        m_crumbLayout->addWidget(more);
        m_segments.append(QList<QWidget*>{ name, more });
    }
    layoutSegments();
}

void AssetPathBar::layoutSegments()
{
    if (m_segments.isEmpty())
    {
        m_overflow->hide();
        return;
    }
    const int available = width() - 40;
    QList<int> widths;
    int total = 0;
    for (const QList<QWidget*>& segment : m_segments)
    {
        int w = 0;
        for (QWidget* widget : segment)
            w += widget->sizeHint().width();
        widths << w;
        total += w;
    }
    // Hide from the left, always keeping the last folder.
    int hidden = 0;
    const int overflowWidth = m_overflow->sizeHint().width();
    while (hidden < m_segments.size() - 1 && total + (hidden > 0 ? overflowWidth : 0) > available)
        total -= widths[hidden++];
    for (int i = 0; i < m_segments.size(); ++i)
        for (QWidget* w : m_segments[i])
            w->setVisible(i >= hidden);
    QMenu* menu = m_overflow->menu();
    menu->clear();
    for (int i = 0; i < hidden; ++i)
    {
        const QString folder = m_folders[i];
        menu->addAction(FolderColors::icon(m_colors->color(folder), 16),
            i == 0 && !m_rootLabel.isEmpty() ? m_rootLabel : QFileInfo(folder).fileName(), this, [this, folder]
            { emit navigate(folder); });
    }
    m_overflow->setVisible(hidden > 0);
}

void AssetPathBar::resizeEvent(QResizeEvent* e)
{
    QWidget::resizeEvent(e);
    layoutSegments();
}

void AssetPathBar::mousePressEvent(QMouseEvent* e)
{
    if (e->button() == Qt::LeftButton && !m_edit->isVisible())
        startEditing();
    QWidget::mousePressEvent(e);
}

void AssetPathBar::startEditing()
{
    QStringList parts;
    for (int i = 0; i < m_folders.size(); ++i)
        parts << (i == 0 && !m_rootLabel.isEmpty() ? m_rootLabel : QFileInfo(m_folders[i]).fileName());
    m_edit->setText(parts.join('/'));
    m_crumbs->hide();
    m_edit->show();
    m_edit->setFocus();
    m_edit->selectAll();
}

void AssetPathBar::finishEditing(bool apply)
{
    if (apply)
    {
        // "Root/a/b", "a/b" (relative to the root) or an absolute folder inside the root
        QString text = QDir::fromNativeSeparators(m_edit->text().trimmed());
        while (text.endsWith('/'))
            text.chop(1);
        QString target;
        if (QDir::isAbsolutePath(text))
            target = text;
        else if (!m_rootLabel.isEmpty() && (text.compare(m_rootLabel, Qt::CaseInsensitive) == 0 || text.startsWith(m_rootLabel + '/', Qt::CaseInsensitive)))
            target = QDir(m_root).filePath(text.mid(m_rootLabel.size() + 1));
        else
            target = QDir(m_root).filePath(text);
        target = QDir::cleanPath(target);
        if (!QFileInfo(target).isDir() || !inside(m_root, target))
        {
            m_edit->setProperty("invalid", true);
            m_edit->style()->unpolish(m_edit);
            m_edit->style()->polish(m_edit);
            return;
        }
        m_edit->hide();
        m_crumbs->show();
        emit navigate(target);
        return;
    }
    m_edit->hide();
    m_crumbs->show();
}

bool AssetPathBar::eventFilter(QObject* watched, QEvent* e)
{
    if (watched == m_edit)
    {
        if (e->type() == QEvent::KeyPress && static_cast<QKeyEvent*>(e)->key() == Qt::Key_Escape)
        {
            finishEditing(false);
            return true;
        }
        if (e->type() == QEvent::FocusOut && m_edit->isVisible())
            finishEditing(false);
    }
    return QWidget::eventFilter(watched, e);
}
