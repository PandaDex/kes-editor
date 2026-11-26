#pragma once
#include <QColor>
#include <QHash>
#include <QImage>
#include <QObject>
#include <QSet>
#include <QStyledItemDelegate>
#include <memory>

// Asset kinds as the browser shows them: a colour for the tile strip and a short label.
namespace AssetTypes
{
    QColor color(const QString& suffix);
    QString label(const QString& suffix);
    bool hasImagePreview(const QString& suffix);
}

// Item roles the asset grid's model provides for AssetTileDelegate.
namespace AssetRoles
{
    enum
    {
        IsFolder = Qt::UserRole + 100,
        FolderColor,  // QColor, invalid = default
        Preview,      // QIcon: material swatch or texture thumbnail, null when there is none (yet)
        Suffix,       // lower-case extension
    };
}

// Texture thumbnails, decoded on the thread pool. ready() fires on the GUI thread.
class ThumbnailCache : public QObject
{
    Q_OBJECT
public:
    explicit ThumbnailCache(QObject* parent = nullptr);
    ~ThumbnailCache() override;

    // Null until loaded; the first call starts loading. Reloads when the file changes.
    QImage thumbnail(const QString& absolutePath);
    void clear();

signals:
    void ready(const QString& absolutePath);

private:
    struct Entry
    {
        QImage image;
        qint64 stamp = 0;
    };
    void deliver(const QString& path, qint64 stamp, const QImage& image);

    QHash<QString, Entry> m_entries;
    QSet<QString> m_pending;
    std::shared_ptr<bool> m_alive;
};

// UE-style asset cards: thumbnail area, type-coloured strip, name and type label.
class AssetTileDelegate : public QStyledItemDelegate
{
    Q_OBJECT
public:
    explicit AssetTileDelegate(QObject* parent = nullptr);

    void setThumbSize(int size) { m_thumb = size; }
    int thumbSize() const { return m_thumb; }
    QSize tileSize() const;

    void paint(QPainter* p, const QStyleOptionViewItem& option, const QModelIndex& index) const override;
    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override;

private:
    int m_thumb = 72;
};
