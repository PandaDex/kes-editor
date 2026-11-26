#include "modules/rmlPreview/RmlPreviewTab.h"
#include "modules/rmlPreview/RmlPreviewView.h"

#include "core/ProjectFile.h"
#include "framework/ContentPaths.h"
#include "po_catalog.h"

#include <QAction>
#include <QComboBox>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QSettings>
#include <QStatusBar>
#include <QToolBar>

#include <algorithm>

namespace rmlp
{
    namespace
    {
        struct Resolution
        {
            const char* label;
            QSize size;
        };

        const Resolution kResolutions[] = {
            { "Fit", {} },
            { "1280 x 720", { 1280, 720 } },
            { "1600 x 900", { 1600, 900 } },
            { "1920 x 1080", { 1920, 1080 } },
            { "2560 x 1440", { 2560, 1440 } },
            { "3840 x 2160", { 3840, 2160 } },
            { "1280 x 800", { 1280, 800 } },
            { "1920 x 1200", { 1920, 1200 } },
            { "2560 x 1080", { 2560, 1080 } },
            { "1024 x 768", { 1024, 768 } },
        };

        const QStringList kDefaultFonts = {
            "engine/ui/fonts/LatoLatin-Regular.ttf",
            "engine/ui/fonts/LatoLatin-Bold.ttf",
            "engine/ui/fonts/LatoLatin-Italic.ttf",
            "engine/ui/fonts/LatoLatin-BoldItalic.ttf",
        };

        QStringList fontList(const QSettings& ini, const QString& key, const QStringList& fallback)
        {
            if (!ini.contains(key))
                return fallback;
            // QSettings already splits a comma list
            QStringList out;
            for (const QString& part : ini.value(key).toStringList())
                for (const QString& item : part.split(',', Qt::SkipEmptyParts))
                    if (const QString trimmed = item.trimmed(); !trimmed.isEmpty())
                        out << trimmed;
            return out;
        }
    }

    RmlPreviewTab::RmlPreviewTab(EditorContext* ctx, QWidget* parent)
        : EditorTab(ctx, QStringLiteral("rmlPreview"), parent)
    {
        m_view = new RmlPreviewView(this, this);
        setCentralWidget(m_view);

        QToolBar* bar = addToolBar(tr("Preview"));
        bar->setObjectName("rmlPreviewToolbar");
        bar->setMovable(false);

        m_resolution = new QComboBox(bar);
        m_resolution->setToolTip(tr("Preview resolution"));
        for (const Resolution& r : kResolutions)
            m_resolution->addItem(tr(r.label), r.size);
        const int savedResolution = QSettings().value("rmlPreview/resolution", 3).toInt();
        m_resolution->setCurrentIndex(std::clamp(savedResolution, 0, m_resolution->count() - 1));
        bar->addWidget(m_resolution);
        connect(m_resolution, &QComboBox::currentIndexChanged, this, [this](int index)
            {
            QSettings().setValue("rmlPreview/resolution", index);
            m_view->setResolution(m_resolution->itemData(index).toSize()); });
        m_view->setResolution(m_resolution->currentData().toSize());

        m_language = new QComboBox(bar);
        m_language->setToolTip(tr("Language for #Key text"));
        bar->addWidget(m_language);
        connect(m_language, &QComboBox::activated, this, [this]
            {
            QSettings().setValue("rmlPreview/language", m_language->currentText());
            loadCatalog();
            reload(); });

        bar->addSeparator();
        QAction* reloadAct = bar->addAction(tr("Reload"), this, [this]
            { reload(); });
        reloadAct->setShortcut(QKeySequence(Qt::Key_F5));
        reloadAct->setShortcutContext(Qt::WidgetWithChildrenShortcut);
        addAction(reloadAct);

        m_watcher = new QFileSystemWatcher(this);
        connect(m_watcher, &QFileSystemWatcher::fileChanged, this, [this]
            { m_reloadTimer.start(); });
        connect(m_watcher, &QFileSystemWatcher::directoryChanged, this, [this]
            { m_reloadTimer.start(); });
        m_reloadTimer.setSingleShot(true);
        m_reloadTimer.setInterval(150);
        connect(&m_reloadTimer, &QTimer::timeout, this, &RmlPreviewTab::onWatchedChange);
        statusBar();
    }

    RmlPreviewTab::~RmlPreviewTab()
    {
        delete m_view;
        m_view = nullptr;
    }

    bool RmlPreviewTab::openFile(const QString& path, QString* error)
    {
        const QFileInfo info(path);
        if (!info.isFile())
        {
            if (error)
                *error = tr("%1 does not exist.").arg(path);
            return false;
        }
        if (!m_view->isReady())
        {
            if (error)
                *error = tr("RmlUi failed to initialise.");
            return false;
        }
        m_path = info.absoluteFilePath();
        loadConfig();
        fillLanguages();
        loadCatalog();
        reload();
        emit titleChanged();
        return true;
    }

    void RmlPreviewTab::assetSaved(const QString& path)
    {
        if (m_dependencies.contains(QFileInfo(path).absoluteFilePath()))
            m_reloadTimer.start();
    }

    QString RmlPreviewTab::resolve(const QString& contentPath) const
    {
        if (QFileInfo(contentPath).isAbsolute())
            return contentPath;
        for (const QString& root : m_searchRoots)
            if (const QFileInfo candidate(QDir(root).filePath(contentPath)); candidate.isFile())
                return candidate.absoluteFilePath();
        return {};
    }

    void RmlPreviewTab::loadConfig()
    {
        // The engine reads one content root where the build has merged engine/assets in, so search
        // the project's content first, then the engine's assets.
        m_contentRoot = forge::findContentRoot(QFileInfo(m_path).absolutePath());
        if (m_contentRoot.isEmpty())
            m_contentRoot = QFileInfo(m_path).absolutePath();
        m_searchRoots = { m_contentRoot };
        m_searchRoots << QDir(QCoreApplication::applicationDirPath()).filePath("engine-assets");
#ifdef KES_ENGINE_ASSETS_DIR
        m_searchRoots << QDir::cleanPath(QStringLiteral(KES_ENGINE_ASSETS_DIR));
#endif

        const QString project = KesProject::findUpwards(QFileInfo(m_path).absolutePath());
        const QString configDir = !project.isEmpty()
            ? QFileInfo(project).absoluteDir().filePath("config")
            : QDir(m_contentRoot).filePath("../config");
        m_configFile = QDir::cleanPath(QDir(configDir).filePath("DefaultUi.ini"));

        QStringList fonts = kDefaultFonts;
        QStringList fallback;
        float density = 1.0f;
        if (QFileInfo::exists(m_configFile))
        {
            const QSettings ini(m_configFile, QSettings::IniFormat);
            fonts = fontList(ini, "UI/Fonts", kDefaultFonts);
            fallback = fontList(ini, "UI/FallbackFonts", {});
            density = ini.value("UI/DensityScale", 1.0).toFloat();
        }
        const auto absolute = [this](const QStringList& paths)
        {
            QStringList out;
            for (const QString& p : paths)
            {
                const QString found = resolve(p);
                if (found.isEmpty())
                    m_pending << Problem{ ProblemSeverity::Warning, tr("Font not found: %1").arg(p), QFileInfo(m_configFile).fileName(), {} };
                else
                    out << found;
            }
            return out;
        };
        m_pending.clear();
        const QStringList fontPaths = absolute(fonts);
        const QStringList fallbackPaths = absolute(fallback);
        {
            SinkScope scope(this);
            m_loading = true;
            runtime::loadFonts(fontPaths, fallbackPaths);
            m_loading = false;
        }
        m_view->setDensityScale(density);
    }

    void RmlPreviewTab::fillLanguages()
    {
        const QString previous = m_language->currentText();
        m_language->clear();
        const QDir dir(QDir(m_contentRoot).filePath("localization"));
        for (const QFileInfo& po : dir.entryInfoList({ "*.po" }, QDir::Files, QDir::Name))
            m_language->addItem(po.completeBaseName(), po.absoluteFilePath());
        m_language->setVisible(m_language->count() > 0);
        QString wanted = previous.isEmpty() ? QSettings().value("rmlPreview/language", "en").toString() : previous;
        int index = m_language->findText(wanted);
        if (index < 0)
            index = m_language->findText("en");
        m_language->setCurrentIndex(index < 0 ? 0 : index);
    }

    void RmlPreviewTab::loadCatalog()
    {
        m_catalog.reset();
        m_catalogPath = m_language->currentData().toString();
        QFile file(m_catalogPath);
        if (m_catalogPath.isEmpty() || !file.open(QIODevice::ReadOnly))
            return;
        auto catalog = std::make_unique<PoCatalog>();
        std::string error;
        const QByteArray text = file.readAll();
        if (!catalog->Parse(std::string_view(text.constData(), size_t(text.size())), &error))
            m_pending << Problem{ ProblemSeverity::Warning, QString::fromStdString(error), QFileInfo(m_catalogPath).fileName(), {} };
        m_catalog = std::move(catalog);
    }

    void RmlPreviewTab::reload()
    {
        if (m_path.isEmpty())
            return;
        ProblemList problems = m_pending;
        m_pending.clear();
        m_dependencies.clear();
        m_loading = true;
        const bool ok = m_view->showDocument(m_path);
        m_loading = false;
        problems << m_pending;
        m_pending = {};
        if (!ok && problems.isEmpty())
            problems << Problem{ ProblemSeverity::Error, tr("Could not load the document."), QFileInfo(m_path).fileName(), {} };
        m_dependencies.insert(m_path);
        if (QFileInfo::exists(m_configFile))
            m_dependencies.insert(m_configFile);
        if (!m_catalogPath.isEmpty())
            m_dependencies.insert(m_catalogPath);
        setProblems(problems);
        watchDependencies();
        showStatus(ok ? tr("Loaded %1").arg(QDateTime::currentDateTime().toString("HH:mm:ss")) : tr("Load failed"), 3000);
    }

    void RmlPreviewTab::watchDependencies()
    {
        if (!m_watcher->files().isEmpty())
            m_watcher->removePaths(m_watcher->files());
        if (!m_watcher->directories().isEmpty())
            m_watcher->removePaths(m_watcher->directories());
        // Folders too: editors that save by replacing the file drop it from the file watch.
        QSet<QString> dirs;
        m_stamps.clear();
        for (const QString& path : m_dependencies)
        {
            const QFileInfo info(path);
            m_stamps.insert(path, info.exists() ? info.lastModified() : QDateTime());
            if (info.exists())
                m_watcher->addPath(path);
            dirs.insert(info.absolutePath());
        }
        for (const QString& dir : dirs)
            if (QFileInfo(dir).isDir())
                m_watcher->addPath(dir);
    }

    void RmlPreviewTab::onWatchedChange()
    {
        bool changed = false;
        for (auto it = m_stamps.cbegin(); it != m_stamps.cend() && !changed; ++it)
        {
            const QFileInfo info(it.key());
            changed = (info.exists() ? info.lastModified() : QDateTime()) != it.value();
        }
        if (!changed)
            return;
        if (!QFileInfo::exists(m_path))
        {
            m_view->closeDocument();
            setProblems({ Problem{ ProblemSeverity::Error, tr("File was removed."), QFileInfo(m_path).fileName(), {} } });
            watchDependencies();
            return;
        }
        const bool configChanged = m_stamps.value(m_configFile) != QFileInfo(m_configFile).lastModified();
        const bool catalogChanged = !m_catalogPath.isEmpty() && m_stamps.value(m_catalogPath) != QFileInfo(m_catalogPath).lastModified();
        if (configChanged)
            loadConfig();
        if (catalogChanged)
            loadCatalog();
        reload();
    }

    void RmlPreviewTab::rmlLog(bool error, const QString& message)
    {
        const QString where = QFileInfo(m_path).fileName();
        for (const Problem& p : m_pending)
            if (p.message == message)
                return;
        if (m_pending.size() >= 200)
            return;
        Problem problem{ error ? ProblemSeverity::Error : ProblemSeverity::Warning, message, where, {} };
        if (m_loading)
        {
            m_pending << problem;
            return;
        }
        // Raised while the document runs (data bindings, events)
        ProblemList current = problems();
        for (const Problem& p : current)
            if (p.message == message)
                return;
        if (current.size() < 200)
        {
            current << problem;
            setProblems(current);
        }
    }

    void RmlPreviewTab::rmlFileRead(const QString& path)
    {
        const QString absolute = QFileInfo(path).absoluteFilePath();
        if (m_dependencies.contains(absolute))
            return;
        m_dependencies.insert(absolute);
        // Images load on first render, after reload() set up the watch
        if (!m_loading)
            watchDependencies();
    }

    void RmlPreviewTab::rmlAction(const QString& name, const QString& args)
    {
        showStatus(args.isEmpty() ? tr("Action: %1").arg(name) : tr("Action: %1 %2").arg(name, args), 3000);
    }
}
