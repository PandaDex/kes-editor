#pragma once
#include <QStringList>
#include <QWidget>

class FolderColors;
class QHBoxLayout;
class QLineEdit;
class QToolButton;

// UE-style breadcrumbs: Root › Folder › Sub. A segment opens its folder, the chevron after it lists
// its subfolders. Clicking the empty part of the bar edits the path as text. Segments that don't
// fit collapse into a leading "…" menu.
class AssetPathBar : public QWidget
{
    Q_OBJECT
public:
    AssetPathBar(FolderColors* colors, QWidget* parent = nullptr);

    // `root` is the top of the browser, `current` a folder inside it.
    void setPath(const QString& root, const QString& rootLabel, const QString& current);

signals:
    void navigate(const QString& folder);

protected:
    void mousePressEvent(QMouseEvent* e) override;
    void resizeEvent(QResizeEvent* e) override;
    bool eventFilter(QObject* watched, QEvent* e) override;

private:
    void rebuild();
    void layoutSegments();
    void startEditing();
    void finishEditing(bool apply);
    QToolButton* chevron(const QString& folder);

    FolderColors* m_colors;
    QString m_root;
    QString m_rootLabel;
    QString m_current;
    QStringList m_folders; // root first
    QWidget* m_crumbs = nullptr;
    QHBoxLayout* m_crumbLayout = nullptr;
    QToolButton* m_overflow = nullptr;
    QList<QList<QWidget*>> m_segments; // per folder: name button + chevron
    QLineEdit* m_edit = nullptr;
};
