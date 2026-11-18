#pragma once
#include <QHash>
#include <QString>
#include <QStringList>

class EditorContext;

namespace AssetFileOps
{
    // Empty when `name` can be used as a file or folder name, otherwise the reason it can't.
    QString checkName(const QString &name);

    // Renames or moves a file or folder. A file's hidden compiled companions (X.fmapc next to
    // X.fmap, X_c.* next to X.*) go with it. `moved` gets old -> new absolute paths of every file
    // that was moved, including the files inside a moved folder. `warnings` gets non-fatal problems.
    bool relocate(const QString &from, const QString &to, QHash<QString, QString> *moved, QString *error,
                  QStringList *warnings);

    // Moves a file or folder, plus a file's hidden compiled companions, to the Recycle Bin.
    // `removed` gets the absolute paths that went.
    bool remove(const QString &path, QStringList *removed, QString *error, QStringList *warnings);

    // Content-relative .mat / .fmap files (outside `paths`) that mention any file in `paths`.
    QStringList findReferences(EditorContext *ctx, const QStringList &paths);

    // Points the open map, the current material and the .mat / .fmap files under the content
    // folder at the new paths. Returns the number of files rewritten on disk.
    int updateReferences(EditorContext *ctx, const QHash<QString, QString> &moved);
}
