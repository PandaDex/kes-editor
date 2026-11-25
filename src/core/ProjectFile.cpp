#include "core/ProjectFile.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QRegularExpression>
#include <QSaveFile>
#include <QTextStream>

namespace
{
    QString unquote(QString value)
    {
        value = value.trimmed();
        if (value.size() >= 2 && value.startsWith('"') && value.endsWith('"'))
            value = value.mid(1, value.size() - 2);
        return value;
    }

    // Parses the first "Project { Key=Value }" block (engine text block syntax, // comments).
    bool parseProjectBlock(const QString &text, QHash<QString, QString> &out)
    {
        bool sawHeader = false, inBlock = false;
        for (QString line : text.split('\n'))
        {
            const int comment = line.indexOf("//");
            if (comment >= 0)
                line = line.left(comment);
            line = line.trimmed();
            if (line.isEmpty())
                continue;
            if (!inBlock)
            {
                if (line == "Project" || line == "Project{" || line.startsWith("Project "))
                    sawHeader = true;
                if (sawHeader && line.endsWith('{'))
                    inBlock = true;
                continue;
            }
            if (line == "}")
                return true;
            const int eq = line.indexOf('=');
            if (eq > 0)
                out.insert(line.left(eq).trimmed(), unquote(line.mid(eq + 1)));
        }
        return inBlock;
    }
}

bool KesProject::load(const QString &path, KesProject &out, QString *error)
{
    out = {};
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        if (error)
            *error = QObject::tr("Cannot open %1: %2").arg(path, file.errorString());
        return false;
    }

    QHash<QString, QString> values;
    if (!parseProjectBlock(QTextStream(&file).readAll(), values))
    {
        if (error)
            *error = QObject::tr("%1 has no Project { ... } block.").arg(path);
        return false;
    }

    const QFileInfo info(path);
    const QDir projectDir = info.absoluteDir();
    out.filePath = info.absoluteFilePath();
    out.name = values.value("Name", info.completeBaseName());
    out.company = values.value("Company");
    out.version = values.value("Version");
    out.startupMap = values.value("StartupMap");
    out.assetRootValue = values.value("AssetRoot", "content");
    out.assetRoot = QDir::cleanPath(projectDir.absoluteFilePath(out.assetRootValue));

    const QDir root(out.assetRoot);
    out.mapsDir = QDir::cleanPath(root.absoluteFilePath("maps"));
    out.materialsDir = QDir::cleanPath(root.absoluteFilePath("materials"));
    out.texturesDir = QDir::cleanPath(root.absoluteFilePath("textures"));
    out.configDir = QDir::cleanPath(projectDir.absoluteFilePath("config"));
    out.entityDefPathsValue = values.value("EntityDefPaths").trimmed();
    if (out.entityDefPathsValue.isEmpty())
        out.entityDefPathsValue = QStringLiteral("editor");
    for (const QString &dir : out.entityDefPathsValue.split(';', Qt::SkipEmptyParts))
        if (!dir.trimmed().isEmpty())
            out.entityDefDirs << QDir::cleanPath(projectDir.absoluteFilePath(dir.trimmed()));

    if (!root.exists())
    {
        if (error)
            *error = QObject::tr("AssetRoot '%1' of %2 does not exist.").arg(out.assetRoot, path);
        out = {};
        return false;
    }
    return true;
}

bool KesProject::writeValues(const QString &path, const QList<QPair<QString, QString>> &values, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
    {
        if (error)
            *error = QObject::tr("Cannot open %1: %2").arg(path, file.errorString());
        return false;
    }
    const QString text = QString::fromUtf8(file.readAll());
    file.close();

    const QString eol = text.contains("\r\n") ? "\r\n" : "\n";
    QStringList lines = text.split(eol);

    // Same block detection as parseProjectBlock: header line, then the line ending in '{'.
    int open = -1, close = -1;
    bool sawHeader = false;
    for (int i = 0; i < lines.size(); ++i)
    {
        QString line = lines[i];
        const int comment = line.indexOf("//");
        if (comment >= 0)
            line = line.left(comment);
        line = line.trimmed();
        if (open < 0)
        {
            if (line == "Project" || line == "Project{" || line.startsWith("Project "))
                sawHeader = true;
            if (sawHeader && line.endsWith('{'))
                open = i;
        }
        else if (line == "}")
        {
            close = i;
            break;
        }
    }
    if (open < 0 || close < 0)
    {
        if (error)
            *error = QObject::tr("%1 has no Project { ... } block.").arg(path);
        return false;
    }

    QString indent = "    ";
    for (const auto &[key, value] : values)
    {
        const QRegularExpression keyLine("^(\\s*)" + QRegularExpression::escape(key) + "\\s*=");
        int found = -1;
        for (int i = open + 1; i < close; ++i)
        {
            const QRegularExpressionMatch m = keyLine.match(lines[i]);
            if (m.hasMatch())
            {
                found = i;
                indent = m.captured(1);
                break;
            }
        }
        if (found >= 0 && value.isEmpty())
        {
            lines.removeAt(found);
            --close;
        }
        else if (found >= 0)
            lines[found] = indent + key + "=\"" + value + "\"";
        else if (!value.isEmpty())
            lines.insert(close++, indent + key + "=\"" + value + "\"");
    }

    QSaveFile out(path);
    if (!out.open(QIODevice::WriteOnly) || out.write(lines.join(eol).toUtf8()) < 0 || !out.commit())
    {
        if (error)
            *error = QObject::tr("Cannot write %1: %2").arg(path, out.errorString());
        return false;
    }
    return true;
}

QString KesProject::findUpwards(const QString &startDir)
{
    QDir dir(startDir);
    for (int depth = 0; depth < 32; ++depth)
    {
        const QStringList found = dir.entryList({"*.kproject"}, QDir::Files, QDir::Name);
        if (!found.isEmpty())
            return dir.absoluteFilePath(found.first());
        if (!dir.cdUp())
            break;
    }
    return {};
}
