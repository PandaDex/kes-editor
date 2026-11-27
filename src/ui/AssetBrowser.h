#pragma once
#include <QPlainTextEdit>
#include <QWidget>
#include <functional>

class AssetFilterModel;
class AssetImporter;
class AssetPathBar;
class AssetTileDelegate;
class FolderColors;
class ThumbnailCache;
class QSlider;
class QToolButton;
class FolderFilterModel;
class EditorContext;
class QDropEvent;
class QFileSystemModel;
class QLabel;
class QListView;
class QMenu;
class QTreeView;
class QLineEdit;
struct AssetTemplate;

class AssetBrowser : public QWidget
{
    Q_OBJECT
public:
    AssetBrowser(EditorContext *ctx, AssetImporter *importer, QWidget *parent = nullptr);

    // Folder shown in the grid, "" when it is the content root (imports then use type folders).
    QString targetFolder() const;
    void openAsset(const QString &path);
    void createFromTemplate(const AssetTemplate &tmpl, const QString &folder);
    void reveal(const QString &path);
    void fillNewMenu(QMenu *menu);
    void setPreviewFilter(std::function<bool(const QString &path)> canPreview) { m_canPreview = std::move(canPreview); }

signals:
    void openAssetRequested(const QString &path);
    void previewAssetRequested(const QString &path);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void setRoot(const QString &dir);
    // Shows `dir` in the grid, syncs the tree and path bar; `record` adds it to back/forward history.
    void showFolder(const QString &dir, bool record = true);
    void goBack();
    void goForward();
    void goUp();
    void updateNavigation();
    void updateCount();
    void setThumbSize(int size);
    void showTreeContextMenu(const QPoint &pos);
    void addFolderActions(QMenu &menu, const QString &folder);
    void addColorMenu(QMenu &menu, const QStringList &folders);
    void openEntry(const QString &path);
    QString materialPath(const QModelIndex &proxyIndex) const;
    void applyToSelection(const QString &material);
    void showContextMenu(const QPoint &pos);
    QStringList selectedPaths() const;
    QStringList draggedEntries(const QDropEvent *e) const;
    bool canMoveInto(const QStringList &entries, const QString &folder) const;
    bool handleTreeDrop(QEvent *event);
    void renameEntry(const QString &path);
    void chooseMoveTarget(const QStringList &paths);
    void moveEntries(const QStringList &paths, const QString &folder);
    void relocate(const QList<QPair<QString, QString>> &moves);
    void deleteEntries(const QStringList &paths);
    void createFolder(const QString &parent);

    EditorContext *m_ctx;
    AssetImporter *m_importer;
    QFileSystemModel *m_dirs;
    FolderFilterModel *m_dirProxy;
    QFileSystemModel *m_files;
    AssetFilterModel *m_proxy;
    QTreeView *m_tree;
    QListView *m_list;
    QLineEdit *m_filter;
    QLabel *m_count = nullptr;
    QSlider *m_zoom = nullptr;
    QToolButton *m_back = nullptr;
    QToolButton *m_forward = nullptr;
    AssetPathBar *m_pathBar = nullptr;
    AssetTileDelegate *m_tiles = nullptr;
    FolderColors *m_colors = nullptr;
    ThumbnailCache *m_thumbs = nullptr;
    QString m_folder;
    QString m_treeRoot;
    QStringList m_history;
    int m_historyPos = -1;
    std::function<bool(const QString &path)> m_canPreview;
};

class ConsolePanel : public QPlainTextEdit
{
    Q_OBJECT
public:
    explicit ConsolePanel(EditorContext *ctx, QWidget *parent = nullptr);
};
