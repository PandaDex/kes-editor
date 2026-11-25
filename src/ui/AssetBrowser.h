#pragma once
#include <QPlainTextEdit>
#include <QWidget>

class AssetFilterModel;
class AssetImporter;
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

signals:
    void openAssetRequested(const QString &path);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void setRoot(const QString &dir);
    void showFolder(const QString &dir);
    void openEntry(const QString &path);
    QString materialPath(const QModelIndex &proxyIndex) const;
    void applyToSelection(const QString &material);
    void showContextMenu(const QPoint &pos);
    void updateCurrent();
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
    QLabel *m_current;
    QString m_folder;
};

class ConsolePanel : public QPlainTextEdit
{
    Q_OBJECT
public:
    explicit ConsolePanel(EditorContext *ctx, QWidget *parent = nullptr);
};
