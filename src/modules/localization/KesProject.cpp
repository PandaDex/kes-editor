#include "KesProject.h"

#include <QDir>
#include <QFileInfo>

namespace kle {

ProjectPaths findProject(const QString& startPath)
{
    ProjectPaths paths;
    const QFileInfo start(startPath);
    if (start.isFile() && start.suffix().compare(QLatin1String("kproject"), Qt::CaseInsensitive) == 0) {
        paths.projectFile = start.absoluteFilePath();
    } else {
        QDir dir(start.isDir() ? start.absoluteFilePath() : start.absolutePath());
        do {
            const QStringList projects = dir.entryList({QStringLiteral("*.kproject")}, QDir::Files);
            if (!projects.isEmpty()) {
                paths.projectFile = dir.absoluteFilePath(projects.first());
                break;
            }
            if (dir.dirName() == QLatin1String("content")) {
                paths.contentRoot = dir.absolutePath();
                QDir parent = dir;
                parent.cdUp();
                paths.projectDir = parent.absolutePath();
                break;
            }
        } while (dir.cdUp());
    }

    if (!paths.projectFile.isEmpty()) {
        paths.projectDir = QFileInfo(paths.projectFile).absolutePath();
        paths.contentRoot = contentRootFromProject(paths.projectFile);
        if (paths.contentRoot.isEmpty()) paths.contentRoot = QDir(paths.projectDir).absoluteFilePath(QStringLiteral("content"));
    }
    if (paths.contentRoot.isEmpty()) return {};
    paths.localizationDir = QDir(paths.contentRoot).absoluteFilePath(QStringLiteral("localization"));
    return paths;
}

} // namespace kle
