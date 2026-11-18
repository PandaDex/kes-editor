#include "assets/AssetFileOps.h"
#include "core/EditorContext.h"
#include "core/MapDocument.h"
#include "core/MaterialLibrary.h"
#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QSet>

namespace
{
    QString tr(const char *text)
    {
        return QCoreApplication::translate("AssetFileOps", text);
    }

    QString cleanAbsolute(const QString &path)
    {
        return QDir::cleanPath(QFileInfo(path).absoluteFilePath());
    }

    // Hidden compiled files that belong to `file`: X.fmapc next to X.fmap, X_c.* next to X.*.
    QStringList companionFiles(const QString &file)
    {
        QStringList out;
        const QFileInfo src(file);
        if (!src.isFile())
            return out;
        const QDir dir = src.absoluteDir();
        const QString base = src.completeBaseName();
        for (const QFileInfo &c : dir.entryInfoList({base + "_c.*"}, QDir::Files | QDir::Hidden))
            if (c.completeBaseName().compare(base + "_c", Qt::CaseInsensitive) == 0)
                out << cleanAbsolute(c.absoluteFilePath());
        if (src.suffix().compare("fmap", Qt::CaseInsensitive) == 0 && QFileInfo::exists(dir.filePath(base + ".fmapc")))
            out << cleanAbsolute(dir.filePath(base + ".fmapc"));
        return out;
    }

    QString lookupKey(QString path)
    {
        return path.replace('\\', '/').toLower();
    }

    bool copyTree(const QString &from, const QString &to)
    {
        if (!QDir().mkpath(to))
            return false;
        const QFileInfoList entries = QDir(from).entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System);
        for (const QFileInfo &entry : entries)
        {
            const QString target = QDir(to).filePath(entry.fileName());
            if (entry.isDir() ? !copyTree(entry.absoluteFilePath(), target) : !QFile::copy(entry.absoluteFilePath(), target))
                return false;
        }
        return true;
    }

    bool moveRaw(const QString &from, const QString &to, QStringList *warnings)
    {
        if (!QFileInfo(from).isDir())
            return QFile::rename(from, to);
        if (QDir().rename(from, to))
            return true;
        // Windows won't rename a folder while a file watcher (the browser's models) holds it open.
        if (!copyTree(from, to))
        {
            QDir(to).removeRecursively();
            return false;
        }
        if (!QDir(from).removeRecursively() && warnings)
            *warnings << tr("Copied to %1 but could not remove all of %2.").arg(QDir::toNativeSeparators(to), QDir::toNativeSeparators(from));
        return true;
    }

    bool moveEntry(const QString &from, const QString &to, QStringList *warnings)
    {
        if (from.compare(to, Qt::CaseInsensitive) != 0)
            return moveRaw(from, to, warnings);
        // Case-only rename: go through a temporary name, case-insensitive file systems see the target as taken.
        const QString temp = to + ".forge-rename";
        if (!moveRaw(from, temp, warnings))
            return false;
        if (moveRaw(temp, to, warnings))
            return true;
        moveRaw(temp, from, warnings);
        return false;
    }

    // Content-relative, lower-cased old path -> new path, for files that stay under `root`.
    QHash<QString, QString> relativeMoves(const QString &root, const QHash<QString, QString> &moved)
    {
        QHash<QString, QString> out;
        if (root.isEmpty())
            return out;
        const QDir dir(root);
        const auto relative = [&dir](const QString &path)
        {
            QString r = dir.relativeFilePath(path).replace('\\', '/');
            return r.startsWith("../") || QDir::isAbsolutePath(r) ? QString() : r;
        };
        for (auto it = moved.cbegin(); it != moved.cend(); ++it)
        {
            const QString from = relative(it.key());
            const QString to = relative(it.value());
            if (!from.isEmpty() && !to.isEmpty())
                out.insert(from.toLower(), to);
        }
        return out;
    }

    bool rewriteFile(const QString &path, const QList<QHash<QString, QString>> &maps)
    {
        QFile in(path);
        if (!in.open(QIODevice::ReadOnly))
            return false;
        const QByteArray original = in.readAll();
        in.close();
        QString text = QString::fromUtf8(original);
        for (const QHash<QString, QString> &map : maps)
            for (auto it = map.cbegin(); it != map.cend(); ++it)
                text.replace('"' + it.key() + '"', '"' + it.value() + '"', Qt::CaseInsensitive);
        const QByteArray updated = text.toUtf8();
        if (updated == original)
            return false;
        QSaveFile out(path);
        if (!out.open(QIODevice::WriteOnly))
            return false;
        out.write(updated);
        return out.commit();
    }
}

QString AssetFileOps::checkName(const QString &name)
{
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty() || trimmed == "." || trimmed == "..")
        return tr("The name is empty.");
    if (trimmed != name)
        return tr("The name must not start or end with spaces.");
    static const QString invalid = QStringLiteral("\\/:*?\"<>|");
    for (const QChar c : name)
        if (invalid.contains(c) || c.unicode() < 32)
            return tr("The name must not contain any of \\ / : * ? \" < > |");
    if (name.endsWith('.'))
        return tr("The name must not end with a dot.");
    return {};
}

bool AssetFileOps::relocate(const QString &from, const QString &to, QHash<QString, QString> *moved, QString *error,
                            QStringList *warnings)
{
    const QFileInfo src(from);
    const QString fromAbs = cleanAbsolute(from);
    const QString toAbs = cleanAbsolute(to);
    const auto fail = [error](const QString &message)
    {
        if (error)
            *error = message;
        return false;
    };
    if (!src.exists())
        return fail(tr("%1 no longer exists.").arg(QDir::toNativeSeparators(fromAbs)));
    if (fromAbs == toAbs)
        return true;
    if (fromAbs.compare(toAbs, Qt::CaseInsensitive) != 0 && QFileInfo::exists(toAbs))
        return fail(tr("%1 already exists.").arg(QDir::toNativeSeparators(toAbs)));
    if (src.isDir() && toAbs.startsWith(fromAbs + '/', Qt::CaseInsensitive))
        return fail(tr("A folder can't be moved into itself."));
    if (!QFileInfo(toAbs).absoluteDir().exists())
        return fail(tr("%1 does not exist.").arg(QDir::toNativeSeparators(QFileInfo(toAbs).absolutePath())));

    QHash<QString, QString> files;
    QList<QPair<QString, QString>> companions;
    if (src.isDir())
    {
        QDirIterator it(fromAbs, QDir::Files | QDir::Hidden | QDir::System, QDirIterator::Subdirectories);
        while (it.hasNext())
        {
            const QString file = QDir::cleanPath(it.next());
            files.insert(file, toAbs + file.mid(fromAbs.size()));
        }
    }
    else
    {
        files.insert(fromAbs, toAbs);
        const QFileInfo dst(toAbs);
        const QString base = src.completeBaseName();
        const QString newBase = dst.completeBaseName();
        for (const QString &c : companionFiles(fromAbs))
            companions.append({c, cleanAbsolute(dst.absoluteDir().filePath(newBase + QFileInfo(c).fileName().mid(base.size())))});
    }

    if (!moveEntry(fromAbs, toAbs, warnings))
        return fail(tr("Could not move %1 to %2. It may be open in another program.")
                        .arg(QDir::toNativeSeparators(fromAbs), QDir::toNativeSeparators(toAbs)));

    for (const auto &[oldPath, newPath] : companions)
    {
        if (oldPath == newPath)
            continue;
        if (oldPath.compare(newPath, Qt::CaseInsensitive) != 0 && QFileInfo::exists(newPath))
        {
            if (warnings)
                *warnings << tr("Left %1 in place: %2 already exists.").arg(QFileInfo(oldPath).fileName(), QDir::toNativeSeparators(newPath));
            continue;
        }
        if (moveEntry(oldPath, newPath, warnings))
            files.insert(oldPath, newPath);
        else if (warnings)
            *warnings << tr("Could not move %1.").arg(QDir::toNativeSeparators(oldPath));
    }

    if (moved)
        moved->insert(files);
    return true;
}

bool AssetFileOps::remove(const QString &path, QStringList *removed, QString *error, QStringList *warnings)
{
    const QString abs = cleanAbsolute(path);
    if (!QFileInfo::exists(abs))
    {
        if (error)
            *error = tr("%1 no longer exists.").arg(QDir::toNativeSeparators(abs));
        return false;
    }
    const QStringList companions = companionFiles(abs);
    if (!QFile::moveToTrash(abs))
    {
        if (error)
            *error = tr("Could not move %1 to the Recycle Bin. It may be open in another program.").arg(QDir::toNativeSeparators(abs));
        return false;
    }
    if (removed)
        *removed << abs;
    for (const QString &c : companions)
    {
        if (QFile::moveToTrash(c))
        {
            if (removed)
                *removed << c;
        }
        else if (warnings)
        {
            *warnings << tr("Could not delete %1.").arg(QDir::toNativeSeparators(c));
        }
    }
    return true;
}

QStringList AssetFileOps::findReferences(EditorContext *ctx, const QStringList &paths)
{
    QStringList out;
    const QString content = ctx->projectDir();
    if (content.isEmpty())
        return out;

    QStringList files;
    for (const QString &path : paths)
    {
        const QString abs = cleanAbsolute(path);
        if (QFileInfo(abs).isDir())
        {
            QDirIterator it(abs, QDir::Files | QDir::Hidden | QDir::System, QDirIterator::Subdirectories);
            while (it.hasNext())
                files << QDir::cleanPath(it.next());
        }
        else
        {
            files << abs;
        }
    }
    QSet<QString> doomed;
    QStringList needles;
    for (const QString &file : files)
    {
        doomed.insert(file.toLower());
        for (const QString &root : {content, ctx->materials()->root()})
        {
            if (root.isEmpty())
                continue;
            const QString relative = QDir(root).relativeFilePath(file).replace('\\', '/');
            if (!relative.startsWith("../") && !QDir::isAbsolutePath(relative))
                needles << '"' + relative + '"';
        }
    }
    needles.removeDuplicates();
    if (needles.isEmpty())
        return out;

    QDirIterator it(content, {"*.mat", "*.fmap"}, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext())
    {
        const QString file = QDir::cleanPath(it.next());
        if (doomed.contains(file.toLower()))
            continue;
        QFile f(file);
        if (!f.open(QIODevice::ReadOnly))
            continue;
        const QString text = QString::fromUtf8(f.readAll());
        for (const QString &needle : needles)
            if (text.contains(needle, Qt::CaseInsensitive))
            {
                out << QDir(content).relativeFilePath(file);
                break;
            }
    }
    return out;
}

int AssetFileOps::updateReferences(EditorContext *ctx, const QHash<QString, QString> &moved)
{
    if (moved.isEmpty())
        return 0;
    const QString content = ctx->projectDir();
    const QHash<QString, QString> contentPaths = relativeMoves(content, moved);
    const QHash<QString, QString> materialPaths = relativeMoves(ctx->materials()->root(), moved);

    MapDocument *doc = ctx->doc();
    if (!doc->filePath().isEmpty())
    {
        const QString docPath = cleanAbsolute(doc->filePath());
        for (auto it = moved.cbegin(); it != moved.cend(); ++it)
            if (it.key().compare(docPath, Qt::CaseInsensitive) == 0)
            {
                doc->setFilePath(it.value());
                break;
            }
    }

    QVector<MapObject> changed;
    for (const auto &[id, object] : doc->objects())
    {
        MapObject copy = object;
        bool dirty = false;
        for (Brush &brush : copy.brushes)
            for (BrushFace &face : brush.faces)
            {
                const auto found = materialPaths.constFind(lookupKey(face.material));
                if (found != materialPaths.cend())
                {
                    face.material = found.value();
                    dirty = true;
                }
            }
        for (auto kv = copy.kv.begin(); kv != copy.kv.end(); ++kv)
        {
            const auto found = contentPaths.constFind(lookupKey(kv.value()));
            if (found != contentPaths.cend())
            {
                kv.value() = found.value();
                dirty = true;
            }
        }
        if (dirty)
            changed.push_back(copy);
    }
    if (!changed.isEmpty())
        doc->modify(changed, tr("Update Asset Paths"));

    const auto current = materialPaths.constFind(lookupKey(ctx->currentMaterial()));
    if (current != materialPaths.cend())
        ctx->setCurrentMaterial(current.value());

    int rewritten = 0;
    if (content.isEmpty() || (contentPaths.isEmpty() && materialPaths.isEmpty()))
        return rewritten;
    QDirIterator it(content, {"*.mat", "*.fmap"}, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext())
    {
        const QString file = it.next();
        if (rewriteFile(file, {contentPaths, materialPaths}))
        {
            ++rewritten;
            ctx->log(tr("Updated paths in %1").arg(QDir(content).relativeFilePath(file)));
        }
    }
    return rewritten;
}
