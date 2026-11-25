#pragma once
#include <QString>

// Content folder helpers for editors that are handed a loose file.
namespace forge
{
    // Absolute content directory named by a .kproject (its AssetRoot).
    QString contentRootFromProject(const QString& kprojectPath);

    // Walks up from a file or directory to the nearest .kproject or "content" folder.
    QString findContentRoot(const QString& startPath);

    // Forward-slash path relative to the content root, empty when outside it.
    QString contentRelativePath(const QString& contentRoot, const QString& absolutePath);
}
