#pragma once
#include <QDialog>

class EditorContext;
class QDialogButtonBox;
class QLabel;
class QLineEdit;
class QPushButton;

// Edit > Project Settings: edits the Project block of the loaded .kproject and reloads it.
class ProjectSettingsDialog : public QDialog
{
    Q_OBJECT
public:
    ProjectSettingsDialog(EditorContext *ctx, QWidget *parent = nullptr);

    void accept() override;

private:
    QString projectDir() const;
    QString contentRoot() const;
    QString validate(QString *warning) const;
    void updateState();
    void browseAssetRoot();
    void browseStartupMap();
    void useCurrentMap();
    // Content-relative .fmapc path for a picked .fmap/.fmapc, or "" if it is outside the content folder.
    QString toStartupMap(const QString &absolutePath) const;

    EditorContext *m_ctx;
    QLineEdit *m_name;
    QLineEdit *m_company;
    QLineEdit *m_version;
    QLineEdit *m_assetRoot;
    QLineEdit *m_startupMap;
    QPushButton *m_currentMap;
    QLabel *m_status;
    QDialogButtonBox *m_buttons;
};
