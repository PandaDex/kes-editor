#pragma once
#include <QString>

struct KesProject;

// Editor-wide preferences in QSettings (not in the .kproject).
namespace editorSettings
{
    enum class StartupMap
    {
        ProjectDefault, // the .kproject's StartupMap (its .fmap source)
        LastOpened      // falls back to the project default
    };

    StartupMap startupMap();
    void setStartupMap(StartupMap mode);

    // Last map opened or saved, per project (keyed by the .kproject, or the content folder without one).
    QString lastMap(const QString& projectKey);
    void setLastMap(const QString& projectKey, const QString& mapPath);

    // Source .fmap of the project's StartupMap, "" when it isn't set or doesn't exist.
    QString projectDefaultMap(const KesProject& project);
}
