#pragma once
#include <QList>
#include <QPair>
#include <QString>

// .kproject, the same file the engine reads (engine/project_file.h). Map asset paths are
// relative to AssetRoot; maps, materials and textures live in fixed folders under it.
struct KesProject
{
    QString filePath;      // absolute path of the .kproject ("" = no project)
    QString name;
    QString company;
    QString version;
    QString assetRootValue; // AssetRoot as written in the file (relative to the .kproject folder)
    QString assetRoot;
    QString startupMap;    // content-relative, as written in the file
    QString mapsDir;
    QString materialsDir;
    QString texturesDir;
    QString configDir;     // <project folder>/config, the folder the engine ships next to the exe

    bool isValid() const { return !filePath.isEmpty(); }

    static bool load(const QString &path, KesProject &out, QString *error = nullptr);
    // Writes the given Key=Value pairs into the Project block of an existing .kproject, keeping
    // every other line. An empty value removes the key; keys not in the file are appended.
    static bool writeValues(const QString &path, const QList<QPair<QString, QString>> &values,
        QString *error = nullptr);
    static QString findUpwards(const QString &startDir);
};
