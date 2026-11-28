#pragma once
#include "texture_format.h"
#include <QImage>
#include <QString>
#include <vector>

struct KesProject;

namespace TextureFile
{
    bool read(const QString &path, TextureFileData &out, QString *error = nullptr, bool keepSource = false);
    bool write(const QString &path, const TextureFileData &texture, QString *error = nullptr);

    // One mip as RGBA8888: BC4 shows as grey, BC5 normal maps with Z rebuilt.
    QImage mipImage(const TextureFileData &texture, uint32_t mip = 0);
    // The embedded source, or mip 0 of a version 1 file (which has none).
    QImage sourceImage(const TextureFileData &texture, QString *error = nullptr);

    // <project>/config/TextureGroups.ini ("" without a project).
    QString groupsPath(const KesProject &project);
    std::vector<TextureGroup> projectGroups(const KesProject &project);

    bool cook(TextureFileData &texture, const QImage &source, const std::vector<TextureGroup> &groups, QString *error = nullptr);
}
