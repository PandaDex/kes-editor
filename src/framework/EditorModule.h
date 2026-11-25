#pragma once
#include <QStringList>
#include <memory>
#include <vector>

class EditorContext;
class EditorTab;

// A kind of editor Forge can open in a tab. Modules are registered in
// modules/BuiltinModules.cpp; each one claims the file extensions it edits.
class EditorModule
{
public:
    virtual ~EditorModule() = default;

    virtual QString id() const = 0;
    virtual QString name() const = 0;
    // lowercase, without the dot
    virtual QStringList extensions() const = 0;
    virtual QString fileFilter() const;

    // Non-empty adds File > New > <label>, which opens an untitled tab.
    virtual QString newLabel() const { return {}; }
    // Non-empty adds Window > <label>, which opens the tab on the current project
    // (openFile() gets the .kproject, or the content folder when there is none).
    virtual QString projectToolLabel() const { return {}; }
    // false: one tab of this kind, reused for every file (the map editor)
    virtual bool allowsMultipleTabs() const { return true; }
    virtual bool closable() const { return true; }

    virtual EditorTab* createTab(EditorContext* ctx) const = 0;
};

class ModuleRegistry
{
public:
    void add(std::unique_ptr<EditorModule> module);

    const EditorModule* module(const QString& id) const;
    const EditorModule* moduleFor(const QString& path) const;
    std::vector<const EditorModule*> modules() const;
    // "All supported (...);;<module filters>"
    QString openFilter() const;

private:
    std::vector<std::unique_ptr<EditorModule>> m_modules;
};

void registerBuiltinModules(ModuleRegistry& registry);
