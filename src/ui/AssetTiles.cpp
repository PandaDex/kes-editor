#include "ui/AssetTiles.h"
#include "core/MaterialLibrary.h"
#include "ui/FolderColors.h"
#include "ui/KesStyle.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QFileInfo>
#include <QPainter>
#include <QPainterPath>
#include <QPointer>
#include <QThreadPool>

#include <algorithm>

namespace AssetTypes
{
    namespace
    {
        struct Kind
        {
            QColor color;
            const char* label;
        };

        const QHash<QString, Kind>& kinds()
        {
            const QColor map(0xe0, 0x5c, 0x4f), model(0x4f, 0x8f, 0xe0), anim(0x9b, 0x6c, 0xe0), texture(0x5c, 0xb8, 0x6a),
                sound(0xe8, 0x95, 0x3c), ui(0x3c, 0xb8, 0xb0), shader(0xe0, 0xc0, 0x4a), material(0xf5, 0xb5, 0x4a),
                text(0xc0, 0x6c, 0x9c), fx(0xf0, 0x7a, 0x4a), data(0x8a, 0x93, 0xa6);
            static const QHash<QString, Kind> table = {
                { "fmap", { map, "Map" } }, { "fmapc", { map, "Compiled Map" } },
                { "kmodel", { model, "Model" } }, { "fbx", { model, "FBX" } }, { "gltf", { model, "glTF" } },
                { "glb", { model, "glTF" } }, { "obj", { model, "OBJ" } },
                { "kanim", { anim, "Animation" } }, { "ag", { anim, "Anim Graph" } }, { "kagraph", { anim, "Anim Graph" } },
                { "ktex", { texture, "Texture" } }, { "png", { texture, "PNG" } }, { "jpg", { texture, "JPEG" } },
                { "jpeg", { texture, "JPEG" } }, { "bmp", { texture, "Bitmap" } }, { "tga", { texture, "TGA" } },
                { "tif", { texture, "TIFF" } }, { "tiff", { texture, "TIFF" } },
                { "ksound", { sound, "Sound" } }, { "wav", { sound, "WAV" } }, { "mp3", { sound, "MP3" } },
                { "ogg", { sound, "OGG" } }, { "flac", { sound, "FLAC" } },
                { "rml", { ui, "UI Document" } }, { "rcss", { ui, "UI Style" } },
                { "po", { text, "Localization" } },
                { "mat", { material, "Material" } },
                { "slangnodes", { shader, "Shader Graph" } }, { "slang", { shader, "Shader" } }, { "shader", { shader, "Shader" } },
                { "kfx", { fx, "Particles" } },
                { "imap", { data, "Input Map" } }, { "kproject", { data, "Project" } }, { "ini", { data, "Config" } },
                { "json", { data, "JSON" } }, { "dec", { data, "Entities" } },
            };
            return table;
        }
    }

    QColor color(const QString& suffix)
    {
        const auto it = kinds().constFind(suffix);
        return it != kinds().cend() ? it->color : QColor(0x5a, 0x61, 0x70);
    }

    QString label(const QString& suffix)
    {
        const auto it = kinds().constFind(suffix);
        return it != kinds().cend() ? QObject::tr(it->label) : suffix.toUpper();
    }

    bool hasImagePreview(const QString& suffix)
    {
        static const QSet<QString> images = { "ktex", "png", "jpg", "jpeg", "bmp", "tga", "tif", "tiff" };
        return images.contains(suffix);
    }
}

ThumbnailCache::ThumbnailCache(QObject* parent) : QObject(parent), m_alive(std::make_shared<bool>(true)) {}

ThumbnailCache::~ThumbnailCache()
{
    *m_alive = false;
}

void ThumbnailCache::clear()
{
    m_entries.clear();
}

QImage ThumbnailCache::thumbnail(const QString& path)
{
    const qint64 stamp = QFileInfo(path).lastModified().toMSecsSinceEpoch();
    const auto it = m_entries.constFind(path);
    if (it != m_entries.cend() && it->stamp == stamp)
        return it->image;
    if (m_pending.contains(path))
        return it != m_entries.cend() ? it->image : QImage();
    m_pending.insert(path);
    QPointer<ThumbnailCache> self(this);
    std::shared_ptr<bool> alive = m_alive;
    QThreadPool::globalInstance()->start([self, alive, path, stamp]
        {
        QImage image = MaterialLibrary::loadTexture(path);
        if (!image.isNull())
            image = image.scaled(256, 256, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        if (!*alive)
            return;
        // Posted to the application object; the cache may be gone by the time it runs.
        QMetaObject::invokeMethod(QCoreApplication::instance(), [self, path, stamp, image]
            {
            if (self) self->deliver(path, stamp, image); }, Qt::QueuedConnection); });
    return it != m_entries.cend() ? it->image : QImage();
}

void ThumbnailCache::deliver(const QString& path, qint64 stamp, const QImage& image)
{
    m_pending.remove(path);
    m_entries.insert(path, Entry{ image, stamp });
    if (!image.isNull())
        emit ready(path);
}

AssetTileDelegate::AssetTileDelegate(QObject* parent) : QStyledItemDelegate(parent) {}

QSize AssetTileDelegate::tileSize() const
{
    return { m_thumb + 28, m_thumb + 62 };
}

QSize AssetTileDelegate::sizeHint(const QStyleOptionViewItem&, const QModelIndex&) const
{
    return tileSize();
}

void AssetTileDelegate::paint(QPainter* p, const QStyleOptionViewItem& option, const QModelIndex& index) const
{
    p->save();
    p->setRenderHint(QPainter::Antialiasing);
    p->setRenderHint(QPainter::SmoothPixmapTransform);

    const bool selected = option.state & QStyle::State_Selected;
    const bool hovered = option.state & QStyle::State_MouseOver;
    const bool folder = index.data(AssetRoles::IsFolder).toBool();
    const QRectF card = QRectF(option.rect).adjusted(4, 4, -4, -4);

    // card
    QColor fill = folder ? QColor(0, 0, 0, 0) : QColor(0x11, 0x14, 0x1b);
    if (selected)
        fill = QColor(0x3a, 0x2f, 0x19);
    else if (hovered)
        fill = QColor(0x1d, 0x22, 0x2c);
    p->setPen(selected ? QPen(kes::style::accent(), 1.2) : folder ? Qt::NoPen : QPen(QColor(0x1c, 0x20, 0x29), 1));
    p->setBrush(fill);
    p->drawRoundedRect(card.adjusted(0.5, 0.5, -0.5, -0.5), 8, 8);

    const QRectF thumb(card.left() + 8, card.top() + 8, card.width() - 16, m_thumb);
    if (folder)
    {
        const QColor color = index.data(AssetRoles::FolderColor).value<QColor>();
        const int size = int(std::min(thumb.width(), thumb.height()));
        FolderColors::icon(color, size).paint(p, thumb.toAlignedRect(), Qt::AlignCenter);
    }
    else
    {
        const QString suffix = index.data(AssetRoles::Suffix).toString();
        const QColor typeColor = AssetTypes::color(suffix);
        QPainterPath clip;
        clip.addRoundedRect(thumb, 6, 6);
        p->setPen(Qt::NoPen);
        p->setBrush(QColor(0x0b, 0x0d, 0x12));
        p->drawPath(clip);

        const QIcon preview = index.data(AssetRoles::Preview).value<QIcon>();
        if (!preview.isNull())
        {
            p->save();
            p->setClipPath(clip);
            preview.paint(p, thumb.adjusted(4, 4, -4, -7).toAlignedRect(), Qt::AlignCenter);
            p->restore();
        }
        else
        {
            QFont f = option.font;
            f.setBold(true);
            f.setPixelSize(std::clamp(int(m_thumb * (suffix.size() > 4 ? 0.17 : 0.22)), 9, 30));
            p->setFont(f);
            p->setPen(typeColor);
            p->drawText(thumb.adjusted(0, 0, 0, -3), Qt::AlignCenter, suffix.toUpper());
        }
        // type strip along the bottom of the thumbnail
        p->save();
        p->setClipPath(clip);
        p->fillRect(QRectF(thumb.left(), thumb.bottom() - 3, thumb.width(), 3), typeColor);
        p->restore();
    }

    // name + type
    const QRectF text(card.left() + 8, thumb.bottom() + 6, card.width() - 16, card.bottom() - thumb.bottom() - 10);
    QFont nameFont = option.font;
    nameFont.setWeight(QFont::DemiBold);
    p->setFont(nameFont);
    const QFontMetricsF nameMetrics(nameFont);
    p->setPen(selected ? kes::style::accent().lighter(130) : QColor(0xf4, 0xf5, 0xf7));
    const QString name = index.data(Qt::DisplayRole).toString();
    const QString shownName = folder ? name : QFileInfo(name).completeBaseName();
    p->drawText(QRectF(text.left(), text.top(), text.width(), nameMetrics.height()), Qt::AlignLeft | Qt::AlignVCenter,
        nameMetrics.elidedText(shownName.isEmpty() ? name : shownName, Qt::ElideMiddle, text.width()));

    QFont typeFont = option.font;
    typeFont.setPointSizeF(typeFont.pointSizeF() * 0.88);
    p->setFont(typeFont);
    p->setPen(kes::style::muted());
    const QString type = folder ? tr("Folder") : AssetTypes::label(index.data(AssetRoles::Suffix).toString());
    p->drawText(QRectF(text.left(), text.top() + nameMetrics.height() + 1, text.width(), QFontMetricsF(typeFont).height()),
        Qt::AlignLeft | Qt::AlignVCenter, QFontMetricsF(typeFont).elidedText(type, Qt::ElideRight, text.width()));
    p->restore();
}
