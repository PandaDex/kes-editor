#include "ui/FolderColors.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QPainter>
#include <QPainterPath>
#include <QSettings>

FolderColors::FolderColors(QObject* parent) : QObject(parent) {}

void FolderColors::setProject(const QString& key, const QString& root)
{
    m_key = QString::fromLatin1(QCryptographicHash::hash(QDir::cleanPath(key).toLower().toUtf8(), QCryptographicHash::Sha1).toHex().left(16));
    m_root = QDir::cleanPath(root);
    m_colors.clear();
    QSettings settings;
    settings.beginGroup(QStringLiteral("folderColors/") + m_key);
    for (const QString& entry : settings.value(QStringLiteral("entries")).toStringList())
    {
        const qsizetype eq = entry.lastIndexOf('=');
        const QColor color(entry.mid(eq + 1));
        if (eq > 0 && color.isValid())
            m_colors.insert(entry.left(eq), color);
    }
    emit changed();
}

QString FolderColors::relative(const QString& absolutePath) const
{
    if (m_root.isEmpty())
        return {};
    const QString rel = QDir(m_root).relativeFilePath(QDir::cleanPath(absolutePath));
    if (rel.startsWith(QStringLiteral("..")) || QDir::isAbsolutePath(rel))
        return {};
    return rel.toLower();
}

QColor FolderColors::color(const QString& absolutePath) const
{
    const QString key = relative(absolutePath);
    return key.isEmpty() && QDir::cleanPath(absolutePath) != m_root ? QColor() : m_colors.value(key);
}

void FolderColors::setColor(const QString& absolutePath, const QColor& color)
{
    const QString key = relative(absolutePath);
    if (key.isEmpty() && QDir::cleanPath(absolutePath) != m_root)
        return;
    if (color.isValid())
        m_colors.insert(key, color);
    else
        m_colors.remove(key);
    save();
}

void FolderColors::moved(const QString& fromAbsolute, const QString& toAbsolute)
{
    const QString from = relative(fromAbsolute);
    const QString to = relative(toAbsolute);
    if (from.isEmpty())
        return;
    QHash<QString, QColor> updated;
    bool any = false;
    for (auto it = m_colors.cbegin(); it != m_colors.cend(); ++it)
    {
        if (it.key() == from || it.key().startsWith(from + '/'))
        {
            any = true;
            if (!to.isEmpty())
                updated.insert(to + it.key().mid(from.size()), it.value());
        }
        else
        {
            updated.insert(it.key(), it.value());
        }
    }
    if (!any)
        return;
    m_colors = updated;
    save();
}

void FolderColors::removed(const QString& absolutePath)
{
    moved(absolutePath, QString());
}

void FolderColors::save()
{
    QStringList entries;
    for (auto it = m_colors.cbegin(); it != m_colors.cend(); ++it)
        entries << it.key() + '=' + it.value().name();
    entries.sort();
    QSettings settings;
    settings.beginGroup(QStringLiteral("folderColors/") + m_key);
    if (entries.isEmpty())
        settings.remove(QStringLiteral("entries"));
    else
        settings.setValue(QStringLiteral("entries"), entries);
    emit changed();
}

QColor FolderColors::defaultColor()
{
    return QColor(0x8a, 0x93, 0xa6);
}

const QList<QPair<QString, QColor>>& FolderColors::presets()
{
    static const QList<QPair<QString, QColor>> colors = {
        { QObject::tr("Amber"), QColor(0xf5, 0xb5, 0x4a) },
        { QObject::tr("Red"), QColor(0xe5, 0x5a, 0x5a) },
        { QObject::tr("Orange"), QColor(0xf0, 0x8a, 0x3c) },
        { QObject::tr("Yellow"), QColor(0xe8, 0xd0, 0x4a) },
        { QObject::tr("Green"), QColor(0x5c, 0xc2, 0x6e) },
        { QObject::tr("Teal"), QColor(0x3c, 0xb8, 0xb0) },
        { QObject::tr("Blue"), QColor(0x5a, 0x9c, 0xf5) },
        { QObject::tr("Purple"), QColor(0xa0, 0x78, 0xf0) },
        { QObject::tr("Pink"), QColor(0xe8, 0x6e, 0xb8) },
        { QObject::tr("White"), QColor(0xe6, 0xe8, 0xec) },
    };
    return colors;
}

QIcon FolderColors::icon(const QColor& requested, int size)
{
    const QColor color = requested.isValid() ? requested : defaultColor();
    static QHash<QString, QIcon> cache;
    const QString key = color.name() + QString::number(size);
    if (auto it = cache.constFind(key); it != cache.cend())
        return it.value();

    QIcon icon;
    for (const qreal dpr : { 1.0, 2.0 })
    {
        QPixmap pixmap(QSize(size, size) * dpr);
        pixmap.setDevicePixelRatio(dpr);
        pixmap.fill(Qt::transparent);
        QPainter p(&pixmap);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(Qt::NoPen);
        const qreal s = size / 64.0;
        const qreal r = std::max(1.5, 5 * s);
        // back with tab, then the front sheet slightly lighter
        QPainterPath back;
        back.setFillRule(Qt::WindingFill);
        back.addRoundedRect(QRectF(6 * s, 12 * s, 22 * s, 10 * s), r * 0.6, r * 0.6);
        back.addRoundedRect(QRectF(6 * s, 16 * s, 52 * s, 38 * s), r, r);
        p.setBrush(color.darker(128));
        p.drawPath(back.simplified());
        p.setBrush(color);
        p.drawRoundedRect(QRectF(6 * s, 22 * s, 52 * s, 32 * s), r, r);
        p.setBrush(QColor(255, 255, 255, 40));
        p.drawRect(QRectF(6 * s + r, 22 * s, 52 * s - 2 * r, std::max(1.0, 1.5 * s)));
        p.end();
        icon.addPixmap(pixmap);
    }
    return cache.insert(key, icon).value();
}
