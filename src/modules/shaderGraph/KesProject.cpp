#include "KesProject.h"

#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>

namespace sne
{

    QString findSlangc(const QStringList& startDirs)
    {
#ifdef Q_OS_WIN
        const QString bundled = QStringLiteral("external/slang/win64/bin/slangc.exe");
        const QString sdkBin = QStringLiteral("Bin/slangc.exe");
#else
        const QString bundled = QStringLiteral("external/slang/linux64/bin/slangc");
        const QString sdkBin = QStringLiteral("bin/slangc");
#endif
        for (const QString& start : startDirs)
        {
            if (start.isEmpty()) continue;
            QDir dir(start);
            do
            {
                if (QFileInfo::exists(dir.absoluteFilePath(bundled))) return dir.absoluteFilePath(bundled);
            } while (dir.cdUp());
        }
        const QString onPath = QStandardPaths::findExecutable(QStringLiteral("slangc"));
        if (!onPath.isEmpty()) return onPath;
        const QString sdk = qEnvironmentVariable("VULKAN_SDK");
        if (!sdk.isEmpty() && QFileInfo::exists(QDir(sdk).absoluteFilePath(sdkBin))) return QDir(sdk).absoluteFilePath(sdkBin);
        return {};
    }

} // namespace sne
