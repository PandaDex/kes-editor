#include "framework/EditorModule.h"
#include <QFileInfo>

QString EditorModule::fileFilter() const
{
    QStringList patterns;
    for (const QString& ext : extensions())
        patterns << "*." + ext;
    return QString("%1 (%2)").arg(name(), patterns.join(' '));
}

void ModuleRegistry::add(std::unique_ptr<EditorModule> module)
{
    m_modules.push_back(std::move(module));
}

const EditorModule* ModuleRegistry::module(const QString& id) const
{
    for (const auto& m : m_modules)
        if (m->id() == id) return m.get();
    return nullptr;
}

const EditorModule* ModuleRegistry::moduleFor(const QString& path) const
{
    const QString suffix = QFileInfo(path).suffix().toLower();
    if (suffix.isEmpty()) return nullptr;
    for (const auto& m : m_modules)
        if (m->extensions().contains(suffix)) return m.get();
    return nullptr;
}

std::vector<const EditorModule*> ModuleRegistry::modules() const
{
    std::vector<const EditorModule*> out;
    for (const auto& m : m_modules)
        out.push_back(m.get());
    return out;
}

QString ModuleRegistry::openFilter() const
{
    QStringList all, filters;
    for (const auto& m : m_modules)
    {
        for (const QString& ext : m->extensions())
            all << "*." + ext;
        filters << m->fileFilter();
    }
    filters.prepend(QObject::tr("All supported (%1)").arg(all.join(' ')));
    filters << QObject::tr("All files (*)");
    return filters.join(";;");
}
