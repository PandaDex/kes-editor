#pragma once

#include "framework/ContentPaths.h"

#include <QStringList>

namespace sne
{

    using forge::contentRelativePath;
    using forge::contentRootFromProject;
    using forge::findContentRoot;

    // Searches each start directory and its parents for the engine's bundled
    // slangc, then PATH and the Vulkan SDK.
    QString findSlangc(const QStringList& startDirs);

} // namespace sne
