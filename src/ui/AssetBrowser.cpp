#include "ui/AssetBrowser.h"
#include "ui/AssetPathBar.h"
#include "ui/AssetTiles.h"
#include "ui/FolderColors.h"
#include "ui/KesStyle.h"
#include "assets/AssetEditors.h"
#include "assets/AssetFileOps.h"
#include "assets/AssetImporter.h"
#include "assets/AssetTemplates.h"
#include "core/EditorContext.h"
#include "tools/MaterialTool.h"
#include <QColorDialog>
#include <QDateTime>
#include <QMouseEvent>
#include <QDesktopServices>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QTimer>
#include <QFileDialog>
#include <QFileSystemModel>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QSettings>
#include <QShortcut>
#include <QSlider>
#include <QSortFilterProxyModel>
#include <QSplitter>
#include <QToolButton>
#include <QTreeView>
#include <QUrl>
#include <QVBoxLayout>

namespace
{
    constexpr int kThumbMin = 48;
    constexpr int kThumbMax = 160;

    bool isCompiledAsset(const QFileInfo &info)
    {
        return info.completeBaseName().endsWith("_c", Qt::CaseInsensitive) ||
               info.suffix().compare("fmapc", Qt::CaseInsensitive) == 0;
    }

    QIcon materialThumbnail(const MaterialInfo &info)
    {
        constexpr int size = 128;
        QPixmap pixmap(size, size);
        if (!info.texture.isNull())
        {
            QImage image = info.texture.scaled(size, size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                               .convertToFormat(QImage::Format_ARGB32);
            QPainter p(&image);
            p.setCompositionMode(QPainter::CompositionMode_Multiply);
            p.fillRect(image.rect(), info.color);
            p.end();
            pixmap = QPixmap::fromImage(image);
        }
        else
        {
            pixmap.fill(info.color);
        }
        return QIcon(pixmap);
    }

    // Drag pixmaps and other places that only take the decoration.
    QIcon typeIcon(const QString &suffix)
    {
        static QHash<QString, QIcon> cache;
        auto cached = cache.constFind(suffix);
        if (cached != cache.cend())
            return cached.value();
        constexpr int size = 64;
        QPixmap pixmap(size, size);
        pixmap.fill(Qt::transparent);
        QPainter p(&pixmap);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0x0b, 0x0d, 0x12));
        p.drawRoundedRect(QRectF(4, 4, size - 8, size - 8), 6, 6);
        p.setBrush(AssetTypes::color(suffix));
        p.drawRect(QRectF(4, size - 8, size - 8, 3));
        QFont font = p.font();
        font.setBold(true);
        font.setPixelSize(suffix.size() > 4 ? 10 : 13);
        p.setFont(font);
        p.setPen(AssetTypes::color(suffix));
        p.drawText(QRectF(4, 4, size - 8, size - 12), Qt::AlignCenter, suffix.toUpper());
        p.end();
        return cache.insert(suffix, QIcon(pixmap)).value();
    }
}

// Thumbnails are made here, on the GUI thread (textures on the thread pool); a QFileSystemModel
// icon provider would run on a worker thread.
class AssetFilterModel : public QSortFilterProxyModel
{
public:
    AssetFilterModel(EditorContext *ctx, QFileSystemModel *files, FolderColors *colors, ThumbnailCache *thumbs, QObject *parent)
        : QSortFilterProxyModel(parent), m_ctx(ctx), m_files(files), m_colors(colors), m_thumbs(thumbs)
    {
        setSourceModel(files);
        QObject::connect(thumbs, &ThumbnailCache::ready, this, [this](const QString &path)
                         {
            const QModelIndex index = mapFromSource(m_files->index(path));
            if (index.isValid())
                emit dataChanged(index, index, {Qt::DecorationRole, AssetRoles::Preview}); });
        QObject::connect(colors, &FolderColors::changed, this, [this]
                         { refreshAll(); });
    }

    QString materialPath(const QModelIndex &index) const
    {
        const QFileInfo info = m_files->fileInfo(mapToSource(index));
        if (!info.isFile() || info.suffix().compare("mat", Qt::CaseInsensitive) != 0)
            return {};
        const QString root = m_ctx->materials()->root();
        if (root.isEmpty())
            return {};
        const QString relative = QDir(root).relativeFilePath(info.absoluteFilePath()).replace('\\', '/');
        return relative.startsWith("../") ? QString() : relative;
    }

    void clearThumbnails()
    {
        m_thumbnails.clear();
        refreshAll();
    }

    void refreshAll()
    {
        const QModelIndex parent = mapFromSource(m_files->index(m_files->rootPath()));
        const int rows = rowCount(parent);
        if (rows > 0)
            emit dataChanged(index(0, 0, parent), index(rows - 1, 0, parent));
    }

    QVariant data(const QModelIndex &index, int role) const override
    {
        if (index.column() != 0)
            return QSortFilterProxyModel::data(index, role);
        switch (role)
        {
        case AssetRoles::IsFolder:
            return m_files->isDir(mapToSource(index));
        case AssetRoles::FolderColor:
            return m_colors->color(m_files->filePath(mapToSource(index)));
        case AssetRoles::Suffix:
            return m_files->fileInfo(mapToSource(index)).suffix().toLower();
        case AssetRoles::Preview:
            return QVariant::fromValue(preview(index));
        case Qt::DecorationRole:
        {
            const QFileInfo info = m_files->fileInfo(mapToSource(index));
            if (info.isDir())
                return FolderColors::icon(m_colors->color(info.absoluteFilePath()), 64);
            const QIcon icon = preview(index);
            return icon.isNull() ? typeIcon(info.suffix().toLower()) : icon;
        }
        case Qt::ToolTipRole:
            return QDir::toNativeSeparators(m_files->filePath(mapToSource(index)));
        default:
            return QSortFilterProxyModel::data(index, role);
        }
    }

    Qt::DropActions supportedDragActions() const override { return Qt::CopyAction; }

protected:
    bool filterAcceptsRow(int row, const QModelIndex &parent) const override
    {
        const QModelIndex index = m_files->index(row, 0, parent);
        const QFileInfo info = m_files->fileInfo(index);
        return info.isDir() || !isCompiledAsset(info);
    }

private:
    QIcon preview(const QModelIndex &index) const
    {
        const QString material = materialPath(index);
        if (!material.isEmpty())
        {
            auto cached = m_thumbnails.constFind(material);
            if (cached != m_thumbnails.cend())
                return cached.value();
            if (const MaterialInfo *info = m_ctx->materials()->find(material))
                return m_thumbnails.insert(material, materialThumbnail(*info)).value();
            return {};
        }
        const QFileInfo info = m_files->fileInfo(mapToSource(index));
        if (info.isFile() && AssetTypes::hasImagePreview(info.suffix().toLower()))
        {
            const QImage image = m_thumbs->thumbnail(info.absoluteFilePath());
            if (!image.isNull())
                return QIcon(QPixmap::fromImage(image));
        }
        return {};
    }

    EditorContext *m_ctx;
    QFileSystemModel *m_files;
    FolderColors *m_colors;
    ThumbnailCache *m_thumbs;
    mutable QHash<QString, QIcon> m_thumbnails;
};

// When the tree is rooted at the project folder, only the listed top-level folders (content, config) are shown.
class FolderFilterModel : public QSortFilterProxyModel
{
public:
    FolderFilterModel(QFileSystemModel *dirs, FolderColors *colors, QObject *parent)
        : QSortFilterProxyModel(parent), m_dirs(dirs), m_colors(colors)
    {
        setSourceModel(dirs);
        QObject::connect(colors, &FolderColors::changed, this, [this]
                         { emit layoutChanged(); });
    }

    void setRoots(const QString &parent, const QStringList &shown)
    {
        beginFilterChange();
        m_parent = parent;
        m_shown = shown;
        endFilterChange(QSortFilterProxyModel::Direction::Rows);
    }

    QVariant data(const QModelIndex &index, int role) const override
    {
        if (role == Qt::DecorationRole && index.column() == 0)
            return FolderColors::icon(m_colors->color(m_dirs->filePath(mapToSource(index))), 16);
        return QSortFilterProxyModel::data(index, role);
    }

protected:
    bool filterAcceptsRow(int row, const QModelIndex &parent) const override
    {
        if (m_parent.isEmpty() || QDir(m_dirs->filePath(parent)) != QDir(m_parent))
            return true;
        const QDir dir(m_dirs->filePath(m_dirs->index(row, 0, parent)));
        for (const QString &shown : m_shown)
            if (dir == QDir(shown))
                return true;
        return false;
    }

private:
    QFileSystemModel *m_dirs;
    FolderColors *m_colors;
    QString m_parent;
    QStringList m_shown;
};

AssetBrowser::AssetBrowser(EditorContext *ctx, AssetImporter *importer, QWidget *parent)
    : QWidget(parent), m_ctx(ctx), m_importer(importer)
{
    m_colors = new FolderColors(this);
    m_thumbs = new ThumbnailCache(this);
    m_dirs = new QFileSystemModel(this);
    m_dirs->setFilter(QDir::AllDirs | QDir::NoDotAndDotDot);
    m_dirProxy = new FolderFilterModel(m_dirs, m_colors, this);
    m_files = new QFileSystemModel(this);
    m_files->setFilter(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot);
    m_files->setNameFilterDisables(false);
    m_proxy = new AssetFilterModel(ctx, m_files, m_colors, m_thumbs, this);

    m_tree = new QTreeView(this);
    m_tree->setObjectName("AssetTree");
    m_tree->setModel(m_dirProxy);
    for (int c = 1; c < 4; ++c)
        m_tree->hideColumn(c);
    m_tree->setHeaderHidden(true);
    m_tree->setIconSize(QSize(16, 16));
    m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
    // Folders in the tree take files dragged from the grid (move) and from outside (import).
    m_tree->setAcceptDrops(true);
    m_tree->viewport()->setAcceptDrops(true);
    m_tree->viewport()->installEventFilter(this);

    m_tiles = new AssetTileDelegate(this);
    m_list = new QListView(this);
    m_list->setObjectName("AssetGrid");
    m_list->setModel(m_proxy);
    m_list->setItemDelegate(m_tiles);
    m_list->setViewMode(QListView::IconMode);
    m_list->setResizeMode(QListView::Adjust);
    m_list->setMovement(QListView::Static);
    m_list->setUniformItemSizes(true);
    m_list->setMouseTracking(true);
    m_list->viewport()->setAttribute(Qt::WA_Hover);
    m_list->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_list->setDragEnabled(true);
    m_list->setDragDropMode(QAbstractItemView::DragOnly);
    m_list->setContextMenuPolicy(Qt::CustomContextMenu);
    auto *renameKey = new QShortcut(QKeySequence(Qt::Key_F2), m_list, nullptr, nullptr, Qt::WidgetShortcut);
    connect(renameKey, &QShortcut::activated, this, [this]
            {
        const QModelIndex index = m_list->currentIndex();
        if (index.isValid())
            renameEntry(m_files->filePath(m_proxy->mapToSource(index))); });
    m_list->installEventFilter(this);
    // External drops are imported and grid items dropped on folders are moved (eventFilter); the view itself never takes drops.
    m_list->setAcceptDrops(true);
    m_list->viewport()->setAcceptDrops(true);
    m_list->viewport()->installEventFilter(this);

    m_filter = new QLineEdit(this);
    m_filter->setObjectName("AssetSearch");
    m_filter->setPlaceholderText(tr("Search"));
    m_filter->addAction(kes::style::icon("search"), QLineEdit::LeadingPosition);
    m_filter->setClearButtonEnabled(true);

    // Top bar: Add, Import | back, forward | path
    auto *addButton = new QToolButton(this);
    addButton->setObjectName("AddButton");
    addButton->setText(tr("+  Add"));
    addButton->setToolTip(tr("Create a folder or an asset from a template"));
    addButton->setPopupMode(QToolButton::InstantPopup);
    auto *newMenu = new QMenu(addButton);
    addButton->setMenu(newMenu);
    connect(newMenu, &QMenu::aboutToShow, this, [this, newMenu]
            {
        newMenu->clear();
        fillNewMenu(newMenu); });

    auto *importButton = new QToolButton(this);
    importButton->setText(tr("Import"));
    importButton->setIcon(kes::style::icon("open"));
    importButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    importButton->setToolTip(tr("Import models, animations, textures and sounds into the project"));
    connect(importButton, &QToolButton::clicked, this, [this]
            { m_importer->importDialog(targetFolder()); });

    m_back = new QToolButton(this);
    m_back->setObjectName("BackButton");
    m_back->setIcon(kes::style::icon("left"));
    m_back->setToolTip(tr("Back (Alt+Left)"));
    m_forward = new QToolButton(this);
    m_forward->setObjectName("ForwardButton");
    m_forward->setIcon(kes::style::icon("right"));
    m_forward->setToolTip(tr("Forward (Alt+Right)"));
    connect(m_back, &QToolButton::clicked, this, &AssetBrowser::goBack);
    connect(m_forward, &QToolButton::clicked, this, &AssetBrowser::goForward);
    for (const auto &[keys, slot] : {std::pair{QKeySequence(Qt::ALT | Qt::Key_Left), &AssetBrowser::goBack},
                                     std::pair{QKeySequence(Qt::ALT | Qt::Key_Right), &AssetBrowser::goForward},
                                     std::pair{QKeySequence(Qt::ALT | Qt::Key_Up), &AssetBrowser::goUp}})
    {
        auto *shortcut = new QShortcut(keys, this, nullptr, nullptr, Qt::WidgetWithChildrenShortcut);
        connect(shortcut, &QShortcut::activated, this, slot);
    }

    m_pathBar = new AssetPathBar(m_colors, this);
    connect(m_pathBar, &AssetPathBar::navigate, this, [this](const QString &folder)
            { showFolder(folder); });

    m_current = new QLabel(this);
    m_current->setObjectName("Muted");
    m_current->setToolTip(tr("Material used by the Material tool and given to new brushes"));
    auto *refresh = new QToolButton(this);
    refresh->setIcon(kes::style::icon("refresh"));
    refresh->setToolTip(tr("Rescan materials and refresh thumbnails"));

    auto *top = new QHBoxLayout;
    top->setContentsMargins(6, 5, 6, 5);
    top->setSpacing(4);
    top->addWidget(addButton);
    top->addWidget(importButton);
    top->addSpacing(8);
    top->addWidget(m_back);
    top->addWidget(m_forward);
    top->addWidget(m_pathBar, 1);
    top->addSpacing(6);
    top->addWidget(m_current);
    top->addWidget(refresh);
    auto *topBar = new QWidget(this);
    topBar->setObjectName("AssetTopBar");
    topBar->setAttribute(Qt::WA_StyledBackground);
    topBar->setLayout(top);

    // Bottom bar: item count | tile size
    m_count = new QLabel(this);
    m_count->setObjectName("Muted");
    m_zoom = new QSlider(Qt::Horizontal, this);
    m_zoom->setRange(kThumbMin, kThumbMax);
    m_zoom->setSingleStep(8);
    m_zoom->setPageStep(16);
    m_zoom->setFixedWidth(110);
    m_zoom->setToolTip(tr("Tile size"));
    connect(m_zoom, &QSlider::valueChanged, this, &AssetBrowser::setThumbSize);
    auto zoomButton = [this](const char *icon, int direction)
    {
        auto *b = new QToolButton(this);
        b->setIcon(kes::style::icon(QString::fromLatin1(icon)));
        b->setAutoRaise(true);
        b->setStyleSheet(QStringLiteral("padding: 1px;"));
        connect(b, &QToolButton::clicked, this, [this, direction]
                { m_zoom->setValue(m_zoom->value() + direction * m_zoom->pageStep()); });
        return b;
    };
    auto *bottom = new QHBoxLayout;
    bottom->setContentsMargins(8, 3, 8, 3);
    bottom->setSpacing(2);
    bottom->addWidget(m_count, 1);
    bottom->addWidget(zoomButton("minus", -1));
    bottom->addWidget(m_zoom);
    bottom->addWidget(zoomButton("plus", +1));
    auto *bottomBar = new QWidget(this);
    bottomBar->setObjectName("AssetBottomBar");
    bottomBar->setAttribute(Qt::WA_StyledBackground);
    bottomBar->setLayout(bottom);

    auto *split = new QSplitter(this);
    split->addWidget(m_tree);
    auto *right = new QWidget(split);
    auto *rl = new QVBoxLayout(right);
    rl->setContentsMargins(6, 6, 6, 0);
    rl->setSpacing(6);
    rl->addWidget(m_filter);
    rl->addWidget(m_list, 1);
    split->addWidget(right);
    split->setStretchFactor(1, 4);
    split->setSizes({200, 800});
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);
    lay->addWidget(topBar);
    lay->addWidget(split, 1);
    lay->addWidget(bottomBar);

    connect(m_tree, &QTreeView::clicked, this, [this](const QModelIndex &i)
            { showFolder(m_dirs->filePath(m_dirProxy->mapToSource(i))); });
    connect(m_tree, &QTreeView::customContextMenuRequested, this, &AssetBrowser::showTreeContextMenu);
    connect(m_filter, &QLineEdit::textChanged, this, [this](const QString &t)
            { m_files->setNameFilters(t.isEmpty() ? QStringList() : QStringList{"*" + t + "*"}); });
    connect(m_list, &QListView::clicked, this, [this](const QModelIndex &i)
            {
        const QString material = materialPath(i);
        if (!material.isEmpty())
            m_ctx->setCurrentMaterial(material); });
    connect(m_list, &QListView::doubleClicked, this, [this](const QModelIndex &i)
            { openEntry(m_files->filePath(m_proxy->mapToSource(i))); });
    connect(m_list, &QListView::customContextMenuRequested, this, &AssetBrowser::showContextMenu);
    connect(m_list->selectionModel(), &QItemSelectionModel::selectionChanged, this, &AssetBrowser::updateCount);
    for (auto signal : {&QAbstractItemModel::rowsInserted, &QAbstractItemModel::rowsRemoved})
        connect(m_proxy, signal, this, &AssetBrowser::updateCount);
    connect(m_proxy, &QAbstractItemModel::layoutChanged, this, &AssetBrowser::updateCount);
    connect(m_proxy, &QAbstractItemModel::modelReset, this, &AssetBrowser::updateCount);
    connect(m_files, &QFileSystemModel::directoryLoaded, this, &AssetBrowser::updateCount);
    connect(refresh, &QToolButton::clicked, this, [this]
            {
        m_thumbs->clear();
        m_ctx->materials()->reload(); });
    connect(ctx->materials(), &MaterialLibrary::changed, this, [this]
            { m_proxy->clearThumbnails(); });
    connect(ctx, &EditorContext::currentMaterialChanged, this, [this]
            { updateCurrent(); });
    connect(ctx, &EditorContext::projectDirChanged, this, [this]
            { setRoot(m_ctx->projectDir()); });
    connect(importer, &AssetImporter::imported, this, [this](const QStringList &outputs)
            {
        if (!outputs.isEmpty())
            reveal(outputs.first()); });

    const int savedThumb = QSettings().value("assetBrowser/tileSize", 72).toInt();
    m_zoom->setValue(std::clamp(savedThumb, kThumbMin, kThumbMax));
    setThumbSize(m_zoom->value());
    setRoot(ctx->projectDir());
    updateCurrent();
}

void AssetBrowser::setRoot(const QString &dir)
{
    // With a project whose config folder sits next to the content folder, the tree shows both.
    const KesProject &project = m_ctx->project();
    const QString parent = QFileInfo(dir).absolutePath();
    const bool withConfig = project.isValid() && QDir(project.configDir).exists() &&
                            QDir(QFileInfo(project.configDir).absolutePath()) == QDir(parent);
    m_treeRoot = withConfig ? parent : dir;
    m_colors->setProject(project.isValid() ? project.filePath : dir, m_treeRoot);

    m_dirProxy->setRoots(withConfig ? parent : QString(), {dir, project.configDir});
    m_dirs->setRootPath(m_treeRoot);
    m_tree->setRootIndex(m_dirProxy->mapFromSource(m_dirs->index(m_treeRoot)));
    const QModelIndex content = m_dirProxy->mapFromSource(m_dirs->index(dir));
    if (withConfig)
        m_tree->expand(content);
    m_history.clear();
    m_historyPos = -1;
    m_folder.clear();
    showFolder(dir);
}

void AssetBrowser::showFolder(const QString &dir, bool record)
{
    const QString clean = QDir::cleanPath(dir);
    if (record && (m_historyPos < 0 || QDir(m_history.value(m_historyPos)) != QDir(clean)))
    {
        m_history = m_history.mid(0, m_historyPos + 1);
        m_history << clean;
        constexpr int kMaxHistory = 64;
        if (m_history.size() > kMaxHistory)
            m_history.removeFirst();
        m_historyPos = int(m_history.size()) - 1;
    }
    m_folder = clean;
    m_files->setRootPath(clean);
    m_list->setRootIndex(m_proxy->mapFromSource(m_files->index(clean)));
    const QModelIndex treeIndex = m_dirProxy->mapFromSource(m_dirs->index(clean));
    if (treeIndex.isValid())
    {
        m_tree->expand(treeIndex.parent());
        m_tree->setCurrentIndex(treeIndex);
        m_tree->scrollTo(treeIndex);
    }
    m_pathBar->setPath(m_treeRoot, QFileInfo(m_treeRoot).fileName(), clean);
    updateNavigation();
    updateCount();
}

void AssetBrowser::goBack()
{
    while (m_historyPos > 0)
    {
        const QString target = m_history.value(--m_historyPos);
        if (QFileInfo(target).isDir())
        {
            showFolder(target, false);
            return;
        }
        m_history.removeAt(m_historyPos);
    }
    updateNavigation();
}

void AssetBrowser::goForward()
{
    while (m_historyPos + 1 < m_history.size())
    {
        const QString target = m_history.value(m_historyPos + 1);
        if (QFileInfo(target).isDir())
        {
            ++m_historyPos;
            showFolder(target, false);
            return;
        }
        m_history.removeAt(m_historyPos + 1);
    }
    updateNavigation();
}

void AssetBrowser::goUp()
{
    if (m_folder.isEmpty() || QDir(m_folder) == QDir(m_treeRoot))
        return;
    showFolder(QFileInfo(m_folder).absolutePath());
}

void AssetBrowser::updateNavigation()
{
    m_back->setEnabled(m_historyPos > 0);
    m_forward->setEnabled(m_historyPos + 1 < m_history.size());
}

void AssetBrowser::updateCount()
{
    const int items = m_proxy->rowCount(m_list->rootIndex());
    int selected = 0;
    for (const QModelIndex &index : m_list->selectionModel()->selectedIndexes())
        if (index.column() == 0)
            ++selected;
    const QString count = items == 1 ? tr("1 item") : tr("%1 items").arg(items);
    m_count->setText(selected > 0 ? tr("%1, %2 selected").arg(count).arg(selected) : count);
}

void AssetBrowser::setThumbSize(int size)
{
    m_tiles->setThumbSize(size);
    m_list->setIconSize(QSize(size, size));
    m_list->setGridSize(m_tiles->tileSize());
    m_list->doItemsLayout();
    QSettings().setValue("assetBrowser/tileSize", size);
}

QString AssetBrowser::targetFolder() const
{
    const QString root = m_ctx->projectDir();
    if (m_folder.isEmpty() || root.isEmpty() || QDir(m_folder) == QDir(root))
        return {};
    return m_folder;
}

void AssetBrowser::openEntry(const QString &path)
{
    if (QFileInfo(path).isDir())
    {
        const QModelIndex index = m_dirProxy->mapFromSource(m_dirs->index(path));
        m_tree->expand(index.parent());
        m_tree->setCurrentIndex(index);
        m_tree->scrollTo(index);
        showFolder(path);
    }
    else
    {
        openAsset(path);
    }
}

void AssetBrowser::openAsset(const QString &path)
{
    if (AssetEditors::open(m_ctx, this, path) == AssetEditors::OpenResult::OpenInForge)
        emit openAssetRequested(path);
}

void AssetBrowser::createFromTemplate(const AssetTemplate &tmpl, const QString &folder)
{
    const QString path = createAsset(m_ctx, this, tmpl, folder);
    if (path.isEmpty())
        return;
    if (tmpl.suffix == "mat")
        m_ctx->materials()->reload();
    reveal(path);
    if (tmpl.openAfterCreate)
        openAsset(path);
}

void AssetBrowser::reveal(const QString &path)
{
    const QString dir = QFileInfo(path).absolutePath();
    m_tree->setCurrentIndex(m_dirProxy->mapFromSource(m_dirs->index(dir)));
    showFolder(dir);
    // The file model fills in the new file asynchronously.
    QTimer::singleShot(250, this, [this, path]
                       {
        const QModelIndex index = m_proxy->mapFromSource(m_files->index(path));
        if (index.isValid())
        {
            m_list->setCurrentIndex(index);
            m_list->scrollTo(index);
        } });
}

void AssetBrowser::fillNewMenu(QMenu *menu)
{
    const QString folder = targetFolder();
    const QString parent = m_folder;
    QAction *newFolder = menu->addAction(tr("Folder"), this, [this, parent]
                                         { createFolder(parent); });
    newFolder->setEnabled(!parent.isEmpty() && QFileInfo(parent).isDir());
    menu->addSeparator();
    for (const AssetTemplate &tmpl : assetTemplates())
        menu->addAction(tmpl.label, this, [this, tmpl, folder]
                        { createFromTemplate(tmpl, folder); });
}

bool AssetBrowser::eventFilter(QObject *watched, QEvent *event)
{
    // Delete in the grid deletes files; taking the shortcut override keeps the map's Delete action from firing.
    if (watched == m_list && (event->type() == QEvent::ShortcutOverride || event->type() == QEvent::KeyPress))
    {
        auto *key = static_cast<QKeyEvent *>(event);
        if (key->matches(QKeySequence::Delete) && key->modifiers() == Qt::NoModifier)
        {
            key->accept();
            if (event->type() == QEvent::KeyPress)
                deleteEntries(selectedPaths());
            return true;
        }
    }
    if ((watched == m_tree->viewport() || watched == m_list->viewport()) && event->type() == QEvent::MouseButtonPress)
    {
        const auto button = static_cast<QMouseEvent *>(event)->button();
        if (button == Qt::BackButton || button == Qt::ForwardButton)
        {
            button == Qt::BackButton ? goBack() : goForward();
            return true;
        }
    }
    if (watched == m_tree->viewport())
        return handleTreeDrop(event) || QWidget::eventFilter(watched, event);
    if (watched != m_list->viewport())
        return QWidget::eventFilter(watched, event);

    if (event->type() == QEvent::DragEnter || event->type() == QEvent::DragMove || event->type() == QEvent::Drop)
    {
        auto *e = static_cast<QDropEvent *>(event);
        const QStringList entries = draggedEntries(e);
        if (!entries.isEmpty())
        {
            const QModelIndex index = m_list->indexAt(e->position().toPoint());
            const QString folder = index.isValid() ? m_files->filePath(m_proxy->mapToSource(index)) : QString();
            const bool movable = canMoveInto(entries, folder);
            // Enter is always accepted, otherwise no move events follow while the cursor looks for a folder.
            if (event->type() == QEvent::DragEnter || movable)
            {
                e->setDropAction(Qt::CopyAction);
                e->accept();
            }
            else
            {
                e->ignore();
            }
            // Moving inside the drop would change the model while the drag is still running.
            if (event->type() == QEvent::Drop && movable)
                QTimer::singleShot(0, this, [this, entries, folder]
                                   { moveEntries(entries, folder); });
            return true;
        }
    }

    const auto importable = [this](const QDropEvent *e)
    {
        QStringList files;
        if (e->source() == m_list || !e->mimeData()->hasUrls())
            return files;
        for (const QUrl &url : e->mimeData()->urls())
            if (url.isLocalFile() && AssetImporter::canImport(url.toLocalFile()))
                files << url.toLocalFile();
        return files;
    };

    switch (event->type())
    {
    case QEvent::DragEnter:
    case QEvent::DragMove:
    {
        auto *e = static_cast<QDropEvent *>(event);
        if (!importable(e).isEmpty())
        {
            e->setDropAction(Qt::CopyAction);
            e->accept();
        }
        else
        {
            e->ignore();
        }
        return true;
    }
    case QEvent::DragLeave:
        return true;
    case QEvent::Drop:
    {
        auto *e = static_cast<QDropEvent *>(event);
        const QStringList files = importable(e);
        if (!files.isEmpty())
        {
            e->setDropAction(Qt::CopyAction);
            e->accept();
            m_importer->import(files, targetFolder());
        }
        return true;
    }
    default:
        return QWidget::eventFilter(watched, event);
    }
}

QString AssetBrowser::materialPath(const QModelIndex &proxyIndex) const
{
    return m_proxy->materialPath(proxyIndex);
}

void AssetBrowser::applyToSelection(const QString &material)
{
    if (!MaterialOps::applyToObjects(m_ctx, m_ctx->doc()->selectionList(), material))
        m_ctx->log(tr("Select one or more solids to apply %1, or drag it onto a face.").arg(material), LogLevel::Warning);
}

void AssetBrowser::showContextMenu(const QPoint &pos)
{
    QMenu menu(this);
    const QModelIndex index = m_list->indexAt(pos);
    if (!index.isValid())
    {
        fillNewMenu(menu.addMenu(tr("New")));
        menu.addAction(tr("Import..."), this, [this]
                       { m_importer->importDialog(targetFolder()); });
        if (!m_folder.isEmpty())
        {
            menu.addSeparator();
            const QString folder = m_folder;
            menu.addAction(tr("Show in Folder"), this, [folder]
                           { QDesktopServices::openUrl(QUrl::fromLocalFile(folder)); });
        }
        menu.exec(m_list->viewport()->mapToGlobal(pos));
        return;
    }
    const QString file = m_files->filePath(m_proxy->mapToSource(index));
    if (!m_list->selectionModel()->isSelected(index))
        m_list->setCurrentIndex(index);
    const QStringList selection = selectedPaths();
    const QStringList targets = selection.contains(file) ? selection : QStringList{file};
    const auto addFileActions = [this, &menu, file, targets]
    {
        QAction *rename = menu.addAction(tr("Rename"), this, [this, file]
                                         { renameEntry(file); });
        rename->setShortcut(QKeySequence(Qt::Key_F2));
        menu.addAction(targets.size() > 1 ? tr("Move %1 Items To...").arg(targets.size()) : tr("Move To..."), this, [this, targets]
                       { chooseMoveTarget(targets); });
        QAction *remove = menu.addAction(targets.size() > 1 ? tr("Delete %1 Items").arg(targets.size()) : tr("Delete"), this, [this, targets]
                                         { deleteEntries(targets); });
        remove->setShortcut(QKeySequence::Delete);
    };
    if (QFileInfo(file).isDir())
    {
        QAction *open = menu.addAction(tr("Open"), this, [this, file]
                                       { openEntry(file); });
        QFont bold = open->font();
        bold.setBold(true);
        open->setFont(bold);
        menu.addSeparator();
        QStringList folders;
        for (const QString &target : targets)
            if (QFileInfo(target).isDir())
                folders << target;
        addColorMenu(menu, folders);
        menu.addSeparator();
        addFileActions();
        menu.addSeparator();
        menu.addAction(tr("Show in Explorer"), this, [file]
                       { QDesktopServices::openUrl(QUrl::fromLocalFile(file)); });
        menu.exec(m_list->viewport()->mapToGlobal(pos));
        return;
    }
    const QString material = materialPath(index);

    const QString editor = AssetEditors::editorLabel(file);
    QAction *open = menu.addAction(editor.isEmpty() ? tr("Open") : tr("Open in %1").arg(editor), this, [this, file]
                                   { openAsset(file); });
    QFont bold = open->font();
    bold.setBold(true);
    open->setFont(bold);
    if (m_canPreview && m_canPreview(file))
        menu.addAction(tr("Preview"), this, [this, file]
                       { emit previewAssetRequested(file); });
    if (AssetImporter::canImport(file))
        menu.addAction(tr("Import into Project"), this, [this, file]
                       { m_importer->import({file}, QFileInfo(file).absolutePath()); });
    menu.addSeparator();
    if (!material.isEmpty())
    {
        menu.addAction(tr("Set as Current Material"), this, [this, material]
                       { m_ctx->setCurrentMaterial(material); });
        menu.addAction(tr("Apply to Selection"), this, [this, material]
                       {
            m_ctx->setCurrentMaterial(material);
            applyToSelection(material); });
        menu.addSeparator();
    }
    addFileActions();
    menu.addSeparator();
    menu.addAction(tr("Show in Folder"), this, [file]
                   { QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(file).absolutePath())); });
    menu.exec(m_list->viewport()->mapToGlobal(pos));
}

QStringList AssetBrowser::selectedPaths() const
{
    QStringList paths;
    for (const QModelIndex &index : m_list->selectionModel()->selectedIndexes())
        if (index.column() == 0)
            paths << m_files->filePath(m_proxy->mapToSource(index));
    return paths;
}

QStringList AssetBrowser::draggedEntries(const QDropEvent *e) const
{
    QStringList paths;
    if (e->source() != m_list || !e->mimeData()->hasUrls())
        return paths;
    for (const QUrl &url : e->mimeData()->urls())
        if (url.isLocalFile())
            paths << url.toLocalFile();
    return paths;
}

bool AssetBrowser::canMoveInto(const QStringList &entries, const QString &folder) const
{
    if (folder.isEmpty() || !QFileInfo(folder).isDir())
        return false;
    const QString target = QDir::cleanPath(QFileInfo(folder).absoluteFilePath());
    bool any = false;
    for (const QString &entry : entries)
    {
        const QFileInfo info(entry);
        const QString path = QDir::cleanPath(info.absoluteFilePath());
        if (info.isDir() && (target.compare(path, Qt::CaseInsensitive) == 0 || target.startsWith(path + '/', Qt::CaseInsensitive)))
            return false;
        if (QDir::cleanPath(info.absolutePath()).compare(target, Qt::CaseInsensitive) != 0)
            any = true;
    }
    return any;
}

bool AssetBrowser::handleTreeDrop(QEvent *event)
{
    // Hovering a drag highlights the folder under the cursor; afterwards the tree goes back to the grid's folder.
    const auto restoreCurrent = [this]
    { m_tree->setCurrentIndex(m_dirProxy->mapFromSource(m_dirs->index(m_folder))); };
    if (event->type() == QEvent::DragLeave)
    {
        restoreCurrent();
        return true;
    }
    if (event->type() != QEvent::DragEnter && event->type() != QEvent::DragMove && event->type() != QEvent::Drop)
        return false;

    auto *e = static_cast<QDropEvent *>(event);
    const QModelIndex index = m_tree->indexAt(e->position().toPoint());
    const QString folder = index.isValid() ? m_dirs->filePath(m_dirProxy->mapToSource(index)) : QString();
    const QStringList entries = draggedEntries(e);
    QStringList imports;
    if (entries.isEmpty() && e->source() != m_list && e->mimeData()->hasUrls())
        for (const QUrl &url : e->mimeData()->urls())
            if (url.isLocalFile() && AssetImporter::canImport(url.toLocalFile()))
                imports << url.toLocalFile();

    const bool move = canMoveInto(entries, folder);
    const bool import = !imports.isEmpty() && !folder.isEmpty();
    if (move || import || (event->type() == QEvent::DragEnter && (!entries.isEmpty() || !imports.isEmpty())))
    {
        e->setDropAction(Qt::CopyAction);
        e->accept();
    }
    else
    {
        e->ignore();
        return true;
    }
    if (event->type() == QEvent::DragMove && index.isValid())
        m_tree->setCurrentIndex(index);
    if (event->type() == QEvent::Drop)
    {
        if (move)
            QTimer::singleShot(0, this, [this, entries, folder]
                               { moveEntries(entries, folder); });
        else if (import)
            m_importer->import(imports, QDir(folder) == QDir(m_ctx->projectDir()) ? QString() : folder);
        restoreCurrent();
    }
    return true;
}

void AssetBrowser::renameEntry(const QString &path)
{
    const QFileInfo info(path);
    if (!info.exists())
        return;
    bool ok = false;
    QString name = QInputDialog::getText(this, info.isDir() ? tr("Rename Folder") : tr("Rename File"), tr("New name:"),
                                         QLineEdit::Normal, info.fileName(), &ok);
    if (!ok || name == info.fileName())
        return;
    if (info.isFile() && !info.suffix().isEmpty() && QFileInfo(name).suffix().isEmpty())
        name += '.' + info.suffix();
    const QString problem = AssetFileOps::checkName(name);
    if (!problem.isEmpty())
    {
        QMessageBox::warning(this, tr("Rename"), problem);
        return;
    }
    const QString newSuffix = QFileInfo(name).suffix();
    if (info.isFile() && newSuffix.compare(info.suffix(), Qt::CaseInsensitive) != 0 &&
        QMessageBox::question(this, tr("Rename"), tr("Change the extension from .%1 to .%2? The file may stop opening or loading.").arg(info.suffix(), newSuffix)) != QMessageBox::Yes)
        return;
    relocate({{path, info.dir().filePath(name)}});
}

void AssetBrowser::createFolder(const QString &parent)
{
    const QDir dir(parent);
    QString suggestion = tr("New Folder");
    for (int i = 2; dir.exists(suggestion); ++i)
        suggestion = tr("New Folder %1").arg(i);
    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("New Folder"), tr("Name:"), QLineEdit::Normal, suggestion, &ok);
    if (!ok)
        return;
    const QString problem = AssetFileOps::checkName(name);
    if (!problem.isEmpty())
    {
        QMessageBox::warning(this, tr("New Folder"), problem);
        return;
    }
    if (dir.exists(name))
    {
        QMessageBox::warning(this, tr("New Folder"), tr("%1 already exists.").arg(name));
        return;
    }
    if (!dir.mkdir(name))
    {
        QMessageBox::warning(this, tr("New Folder"), tr("Could not create %1.").arg(QDir::toNativeSeparators(dir.filePath(name))));
        return;
    }
    m_ctx->log(tr("Created folder %1").arg(QDir::toNativeSeparators(QDir(m_ctx->projectDir()).relativeFilePath(dir.filePath(name)))));
    reveal(dir.filePath(name));
}

void AssetBrowser::chooseMoveTarget(const QStringList &paths)
{
    const QString content = m_ctx->projectDir();
    const QString folder = QFileDialog::getExistingDirectory(this, tr("Move To"), m_folder.isEmpty() ? content : m_folder);
    if (folder.isEmpty())
        return;
    const auto inside = [&folder](const QString &root)
    {
        if (root.isEmpty())
            return false;
        const QString relative = QDir(root).relativeFilePath(folder);
        return !relative.startsWith("..") && !QDir::isAbsolutePath(relative);
    };
    if (!inside(content) && !inside(m_ctx->project().configDir))
    {
        QMessageBox::warning(this, tr("Move To"), tr("Pick a folder inside the project's content or config folder."));
        return;
    }
    if (!canMoveInto(paths, folder))
    {
        QMessageBox::information(this, tr("Move To"), tr("Nothing to move: the selection is already in that folder, or a folder would be moved into itself."));
        return;
    }
    moveEntries(paths, folder);
}

void AssetBrowser::moveEntries(const QStringList &paths, const QString &folder)
{
    QList<QPair<QString, QString>> moves;
    const QDir target(folder);
    for (const QString &path : paths)
    {
        const QFileInfo info(path);
        if (info.exists() && info.absoluteDir() != target)
            moves.append({path, target.filePath(info.fileName())});
    }
    relocate(moves);
}

void AssetBrowser::relocate(const QList<QPair<QString, QString>> &moves)
{
    if (moves.isEmpty())
        return;
    const QDir content(m_ctx->projectDir());
    const auto shown = [&content](const QString &path)
    { return QDir::toNativeSeparators(content.relativeFilePath(path)); };

    QHash<QString, QString> moved;
    QStringList warnings;
    QStringList errors;
    QString renamed;
    bool folderMoved = false;
    for (const auto &[from, to] : moves)
    {
        const QString fromClean = QDir::cleanPath(QFileInfo(from).absoluteFilePath());
        const bool sameFolder = QFileInfo(from).absoluteDir() == QFileInfo(to).absoluteDir();
        QString error;
        const bool isFolder = QFileInfo(from).isDir();
        if (!AssetFileOps::relocate(from, to, &moved, &error, &warnings))
        {
            errors << error;
            continue;
        }
        if (isFolder)
            m_colors->moved(from, to);
        m_ctx->log(sameFolder ? tr("Renamed %1 to %2").arg(shown(from), QFileInfo(to).fileName())
                              : tr("Moved %1 to %2").arg(shown(from), shown(to)));
        if (sameFolder)
            renamed = to;
        const QString current = QDir::cleanPath(m_folder);
        if (current.compare(fromClean, Qt::CaseInsensitive) == 0 || current.startsWith(fromClean + '/', Qt::CaseInsensitive))
        {
            m_folder = QDir::cleanPath(to) + current.mid(fromClean.size());
            folderMoved = true;
        }
    }
    for (const QString &warning : warnings)
        m_ctx->log(warning, LogLevel::Warning);

    if (!moved.isEmpty())
    {
        AssetFileOps::updateReferences(m_ctx, moved);
        m_ctx->materials()->reload();
        if (folderMoved)
        {
            m_tree->setCurrentIndex(m_dirProxy->mapFromSource(m_dirs->index(m_folder)));
            showFolder(m_folder);
        }
        else if (moves.size() == 1 && !renamed.isEmpty())
        {
            reveal(renamed);
        }
    }
    if (!errors.isEmpty())
        QMessageBox::warning(this, moves.size() == 1 ? tr("Rename / Move") : tr("Move"), errors.join('\n'));
}

void AssetBrowser::deleteEntries(const QStringList &paths)
{
    if (paths.isEmpty())
        return;
    const QDir content(m_ctx->projectDir());
    const auto shown = [&content](const QString &path)
    { return QDir::toNativeSeparators(content.relativeFilePath(path)); };

    QString question = paths.size() == 1
                           ? tr("Move %1 to the Recycle Bin?").arg(shown(paths.first()))
                           : tr("Move these %1 items to the Recycle Bin?").arg(paths.size());
    const QString docPath = m_ctx->doc()->filePath();
    bool deletesOpenMap = false;
    for (const QString &path : paths)
    {
        const QString clean = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
        const QString doc = docPath.isEmpty() ? QString() : QDir::cleanPath(QFileInfo(docPath).absoluteFilePath());
        if (!doc.isEmpty() && (doc.compare(clean, Qt::CaseInsensitive) == 0 || doc.startsWith(clean + '/', Qt::CaseInsensitive)))
            deletesOpenMap = true;
    }
    if (deletesOpenMap)
        question += "\n\n" + tr("This includes the open map. It stays open; saving writes it back.");
    const QStringList references = AssetFileOps::findReferences(m_ctx, paths);
    if (!references.isEmpty())
    {
        constexpr int kShown = 10;
        QStringList listed = references.mid(0, kShown);
        for (QString &r : listed)
            r = "  " + QDir::toNativeSeparators(r);
        if (references.size() > kShown)
            listed << tr("  ...and %1 more").arg(references.size() - kShown);
        question += "\n\n" + tr("Still used by:") + '\n' + listed.join('\n');
    }
    if (QMessageBox::question(this, tr("Delete"), question, QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
        return;

    QStringList removed;
    QStringList warnings;
    QStringList errors;
    bool folderGone = false;
    for (const QString &path : paths)
    {
        const QString clean = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
        QString error;
        const bool isFolder = QFileInfo(path).isDir();
        if (!AssetFileOps::remove(path, &removed, &error, &warnings))
        {
            errors << error;
            continue;
        }
        if (isFolder)
            m_colors->removed(path);
        m_ctx->log(tr("Deleted %1").arg(shown(path)));
        const QString current = QDir::cleanPath(m_folder);
        if (current.compare(clean, Qt::CaseInsensitive) == 0 || current.startsWith(clean + '/', Qt::CaseInsensitive))
        {
            m_folder = QFileInfo(clean).absolutePath();
            folderGone = true;
        }
    }
    for (const QString &warning : warnings)
        m_ctx->log(warning, LogLevel::Warning);
    for (const QString &reference : references)
        m_ctx->log(tr("%1 still references a deleted file").arg(QDir::toNativeSeparators(reference)), LogLevel::Warning);

    if (!removed.isEmpty())
    {
        m_ctx->materials()->reload();
        if (folderGone)
        {
            m_tree->setCurrentIndex(m_dirProxy->mapFromSource(m_dirs->index(m_folder)));
            showFolder(m_folder);
        }
    }
    if (!errors.isEmpty())
        QMessageBox::warning(this, tr("Delete"), errors.join('\n'));
}

void AssetBrowser::addColorMenu(QMenu &menu, const QStringList &folders)
{
    if (folders.isEmpty())
        return;
    QMenu *colors = menu.addMenu(FolderColors::icon(m_colors->color(folders.first()), 16), tr("Set Color"));
    for (const auto &[name, color] : FolderColors::presets())
        colors->addAction(FolderColors::icon(color, 16), name, this, [this, folders, color]
                          {
            for (const QString &folder : folders)
                m_colors->setColor(folder, color); });
    colors->addSeparator();
    colors->addAction(tr("Custom..."), this, [this, folders]
                      {
        const QColor current = m_colors->color(folders.first());
        const QColor chosen = QColorDialog::getColor(current.isValid() ? current : FolderColors::defaultColor(), this, tr("Folder Color"));
        if (chosen.isValid())
            for (const QString &folder : folders)
                m_colors->setColor(folder, chosen); });
    bool anyColored = false;
    for (const QString &folder : folders)
        anyColored |= m_colors->color(folder).isValid();
    QAction *clear = colors->addAction(tr("Clear Color"), this, [this, folders]
                                       {
        for (const QString &folder : folders)
            m_colors->setColor(folder, QColor()); });
    clear->setEnabled(anyColored);
}

void AssetBrowser::addFolderActions(QMenu &menu, const QString &folder)
{
    menu.addAction(tr("New Folder..."), this, [this, folder]
                   { createFolder(folder); });
    QMenu *add = menu.addMenu(tr("New Asset"));
    const QString target = QDir(folder) == QDir(m_ctx->projectDir()) ? QString() : folder;
    for (const AssetTemplate &tmpl : assetTemplates())
        add->addAction(tmpl.label, this, [this, tmpl, target]
                       { createFromTemplate(tmpl, target); });
    menu.addAction(tr("Import..."), this, [this, target]
                   { m_importer->importDialog(target); });
    menu.addSeparator();
    addColorMenu(menu, {folder});
}

void AssetBrowser::showTreeContextMenu(const QPoint &pos)
{
    const QModelIndex index = m_tree->indexAt(pos);
    if (!index.isValid())
        return;
    const QString folder = m_dirs->filePath(m_dirProxy->mapToSource(index));
    const bool isRoot = QDir(folder) == QDir(m_ctx->projectDir()) || QDir(folder) == QDir(m_ctx->project().configDir);
    QMenu menu(this);
    addFolderActions(menu, folder);
    menu.addSeparator();
    if (!isRoot)
    {
        menu.addAction(tr("Rename"), this, [this, folder]
                       { renameEntry(folder); });
        menu.addAction(tr("Delete"), this, [this, folder]
                       { deleteEntries({folder}); });
        menu.addSeparator();
    }
    menu.addAction(tr("Show in Explorer"), this, [folder]
                   { QDesktopServices::openUrl(QUrl::fromLocalFile(folder)); });
    menu.exec(m_tree->viewport()->mapToGlobal(pos));
}

void AssetBrowser::updateCurrent()
{
    const QString current = m_ctx->currentMaterial();
    m_current->setText(current.isEmpty() ? tr("Material: (none)") : tr("Material: %1").arg(QFileInfo(current).completeBaseName()));
}

ConsolePanel::ConsolePanel(EditorContext *ctx, QWidget *parent) : QPlainTextEdit(parent)
{
    setReadOnly(true);
    setMaximumBlockCount(5000);
    setFont(QFont("monospace", 9));
    connect(ctx, &EditorContext::logMessage, this, [this](const QString &text, int level)
            {
        static const char* colors[3] = {"#dfe2e8", "#f2c94c", "#f26d6d"};
        appendHtml(QString("<span style='color:#7a8090'>%1</span> <span style='color:%2'>%3</span>")
                       .arg(QDateTime::currentDateTime().toString("HH:mm:ss"), colors[std::clamp(level, 0, 2)], text.toHtmlEscaped())); });
}
