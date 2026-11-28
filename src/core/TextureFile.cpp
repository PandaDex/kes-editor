#include "core/TextureFile.h"
#include "core/ProjectFile.h"
#include "texture_cook.h"
#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QSaveFile>
#include <cstring>

namespace TextureFile
{
    bool read(const QString &path, TextureFileData &out, QString *error, bool keepSource)
    {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly))
        {
            if (error)
                *error = file.errorString();
            return false;
        }
        const QByteArray bytes = file.readAll();
        std::string message;
        if (!ReadKTex(reinterpret_cast<const uint8_t *>(bytes.constData()), size_t(bytes.size()), out, &message, keepSource))
        {
            if (error)
                *error = QString::fromStdString(message);
            return false;
        }
        return true;
    }

    bool write(const QString &path, const TextureFileData &texture, QString *error)
    {
        std::vector<uint8_t> bytes;
        std::string message;
        if (!WriteKTex(texture, bytes, &message))
        {
            if (error)
                *error = QString::fromStdString(message);
            return false;
        }
        QSaveFile file(path);
        if (!file.open(QIODevice::WriteOnly) ||
            file.write(reinterpret_cast<const char *>(bytes.data()), qint64(bytes.size())) != qint64(bytes.size()) || !file.commit())
        {
            if (error)
                *error = file.errorString();
            return false;
        }
        return true;
    }

    QImage mipImage(const TextureFileData &texture, uint32_t mip)
    {
        std::vector<uint8_t> rgba;
        if (!DecodeTextureMip(texture, mip, rgba))
            return {};
        const int width = int(TextureMipWidth(texture.width, mip));
        const int height = int(TextureMipWidth(texture.height, mip));
        QImage image(width, height, QImage::Format_RGBA8888);
        for (int y = 0; y < height; ++y)
            memcpy(image.scanLine(y), rgba.data() + size_t(y) * width * 4, size_t(width) * 4);
        return image;
    }

    QImage sourceImage(const TextureFileData &texture, QString *error)
    {
        if (!texture.sourceData.empty())
        {
            QImage image = QImage::fromData(texture.sourceData.data(), int(texture.sourceData.size()));
            if (image.isNull() && error)
                *error = QObject::tr("the embedded source image can't be decoded");
            return image.isNull() ? image : image.convertToFormat(QImage::Format_RGBA8888);
        }
        if (texture.version < 2)
            return mipImage(texture, 0);
        if (error)
            *error = QObject::tr("the texture has no source image");
        return {};
    }

    QString groupsPath(const KesProject &project)
    {
        return project.configDir.isEmpty() ? QString() : QDir(project.configDir).filePath(QStringLiteral("TextureGroups.ini"));
    }

    std::vector<TextureGroup> projectGroups(const KesProject &project)
    {
        std::vector<TextureGroup> groups = DefaultTextureGroups();
        QFile file(groupsPath(project));
        if (!groupsPath(project).isEmpty() && file.open(QIODevice::ReadOnly))
            ParseTextureGroups(file.readAll().toStdString(), groups);
        return groups;
    }

    bool cook(TextureFileData &texture, const QImage &source, const std::vector<TextureGroup> &groups, QString *error)
    {
        if (source.isNull())
        {
            if (error)
                *error = QObject::tr("no source image");
            return false;
        }
        const QImage rgba = source.convertToFormat(QImage::Format_RGBA8888);
        std::vector<uint8_t> pixels(size_t(rgba.width()) * rgba.height() * 4);
        for (int y = 0; y < rgba.height(); ++y)
            memcpy(pixels.data() + size_t(y) * rgba.width() * 4, rgba.constScanLine(y), size_t(rgba.width()) * 4);

        // A version 1 texture becomes version 2 here; its old pixels are the best source there is.
        if (texture.sourceData.empty())
        {
            QByteArray png;
            QBuffer buffer(&png);
            buffer.open(QIODevice::WriteOnly);
            rgba.save(&buffer, "PNG");
            texture.sourceData.assign(png.begin(), png.end());
            if (texture.sourcePath.empty())
                texture.sourcePath = "source.png";
        }

        std::string message;
        if (!CookTexture(pixels.data(), uint32_t(rgba.width()), uint32_t(rgba.height()),
                ResolveTextureSettings(texture.settings, groups), texture, &message))
        {
            if (error)
                *error = QString::fromStdString(message);
            return false;
        }
        texture.version = 2;
        return true;
    }
}
