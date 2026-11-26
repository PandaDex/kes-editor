#pragma once

#include "framework/EditorModule.h"
#include "framework/EditorTab.h"
#include "modules/rmlPreview/RmlRuntime.h"

#include <QDateTime>
#include <QHash>
#include <QSet>
#include <QTimer>
#include <memory>

class PoCatalog;
class QComboBox;
class QFileSystemWatcher;

namespace rmlp
{
    class RmlPreviewView;

    // Read-only live view of an .rml document, reloaded whenever it or anything it loads changes.
    class RmlPreviewTab : public EditorTab, public RmlSink
    {
        Q_OBJECT
    public:
        explicit RmlPreviewTab(EditorContext* ctx, QWidget* parent = nullptr);
        ~RmlPreviewTab() override;

        QString filePath() const override { return m_path; }
        bool isModified() const override { return false; }
        bool openFile(const QString& path, QString* error) override;
        bool save() override { return true; }
        bool saveAs() override { return true; }
        void assetSaved(const QString& path) override;

        void rmlLog(bool error, const QString& message) override;
        void rmlFileRead(const QString& path) override;
        const PoCatalog* rmlCatalog() const override { return m_catalog.get(); }
        QStringList rmlSearchRoots() const override { return m_searchRoots; }
        void rmlAction(const QString& name, const QString& args) override;

    private:
        void loadConfig();
        void fillLanguages();
        void loadCatalog();
        void reload();
        void watchDependencies();
        void onWatchedChange();
        QString resolve(const QString& contentPath) const;

        RmlPreviewView* m_view = nullptr;
        QComboBox* m_resolution = nullptr;
        QComboBox* m_language = nullptr;
        QFileSystemWatcher* m_watcher = nullptr;
        QTimer m_reloadTimer;

        QString m_path;
        QString m_contentRoot;
        QString m_configFile;
        QStringList m_searchRoots;
        std::unique_ptr<PoCatalog> m_catalog;
        QString m_catalogPath;
        QSet<QString> m_dependencies;
        QHash<QString, QDateTime> m_stamps;
        ProblemList m_pending;
        bool m_loading = false;
    };

    class RmlPreviewModule : public EditorModule
    {
    public:
        QString id() const override { return QStringLiteral("rmlPreview"); }
        QString name() const override { return QStringLiteral("UI Preview"); }
        QStringList extensions() const override { return {}; }
        QStringList previewExtensions() const override { return { QStringLiteral("rml") }; }
        EditorTab* createTab(EditorContext* ctx) const override { return new RmlPreviewTab(ctx); }
    };
}
