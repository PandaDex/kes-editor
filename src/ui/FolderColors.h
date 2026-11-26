#pragma once
#include <QColor>
#include <QHash>
#include <QIcon>
#include <QObject>
#include <QString>

// Per-user, per-project folder colours for the Asset Browser (like UE's folder colours). Stored in
// QSettings under the project, keyed by the folder's path relative to the browser root, so they
// never end up in the game's content.
class FolderColors : public QObject
{
    Q_OBJECT
public:
    explicit FolderColors(QObject* parent = nullptr);

    // `key` names the project (its .kproject path, or the content folder without one).
    void setProject(const QString& key, const QString& root);

    QColor color(const QString& absolutePath) const;
    void setColor(const QString& absolutePath, const QColor& color); // invalid colour clears
    // Folders renamed or moved (and their subfolders) keep their colours; removed ones lose them.
    void moved(const QString& fromAbsolute, const QString& toAbsolute);
    void removed(const QString& absolutePath);

    // Folder glyph in `color` (the default grey when invalid), `size` pixels square.
    static QIcon icon(const QColor& color, int size);
    static QColor defaultColor();
    // Swatches offered in Set Color.
    static const QList<QPair<QString, QColor>>& presets();

signals:
    void changed();

private:
    QString relative(const QString& absolutePath) const;
    void save();

    QString m_key;
    QString m_root;
    QHash<QString, QColor> m_colors; // lower-case relative path -> colour
};
