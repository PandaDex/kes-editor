#include "framework/EditorSettings.h"
#include "core/ProjectFile.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QSettings>

namespace
{
    QString lastMapKey(const QString& projectKey)
    {
        const QByteArray normalized = QDir::cleanPath(QFileInfo(projectKey).absoluteFilePath()).toLower().toUtf8();
        return "lastMap/" + QCryptographicHash::hash(normalized, QCryptographicHash::Md5).toHex();
    }
}

namespace editorSettings
{
    StartupMap startupMap()
    {
        return QSettings().value("editor/openMapOnStartup").toString() == QLatin1String("projectDefault")
            ? StartupMap::ProjectDefault
            : StartupMap::LastOpened;
    }

    void setStartupMap(StartupMap mode)
    {
        QSettings().setValue("editor/openMapOnStartup", mode == StartupMap::ProjectDefault ? "projectDefault" : "lastOpened");
    }

    QString lastMap(const QString& projectKey)
    {
        return projectKey.isEmpty() ? QString() : QSettings().value(lastMapKey(projectKey)).toString();
    }

    void setLastMap(const QString& projectKey, const QString& mapPath)
    {
        if (!projectKey.isEmpty() && !mapPath.isEmpty())
            QSettings().setValue(lastMapKey(projectKey), QFileInfo(mapPath).absoluteFilePath());
    }

    QString projectDefaultMap(const KesProject& project)
    {
        if (!project.isValid() || project.startupMap.trimmed().isEmpty())
            return {};
        const QFileInfo compiled(QDir(project.assetRoot).filePath(project.startupMap.trimmed()));
        const QString source = compiled.suffix().compare("fmapc", Qt::CaseInsensitive) == 0
            ? compiled.dir().filePath(compiled.completeBaseName() + ".fmap")
            : compiled.absoluteFilePath();
        return QFileInfo::exists(source) ? QFileInfo(source).absoluteFilePath() : QString();
    }
}
