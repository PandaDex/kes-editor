#pragma once
#include <QDialog>
#include <QStringList>

class QCheckBox;
class QComboBox;
class QDialogButtonBox;
class QLineEdit;
class QPushButton;
class QSpinBox;

struct ModelImportChoice
{
    bool mesh = true;
    bool animations = true;
    // Animation-only imports: the .kmodel the clips belong to. Empty = named after each source file.
    QString targetModel;
    // -1 keeps what an existing .kmodel has, 0 none, n generated levels (model-importer --lods)
    int lodCount = -1;
};

class ModelImportDialog : public QDialog
{
    Q_OBJECT
public:
    ModelImportDialog(const QStringList &files, const QString &projectDir, QWidget *parent = nullptr);

    ModelImportChoice choice() const;

    // Shows the dialog and remembers the choice for next time. False if cancelled.
    static bool ask(const QStringList &files, const QString &projectDir, QWidget *parent, ModelImportChoice &out);

private:
    void updateState();
    void browseTarget();

    QString m_projectDir;
    QCheckBox *m_mesh;
    QCheckBox *m_animations;
    QLineEdit *m_target;
    QComboBox *m_lodMode;
    QSpinBox *m_lodCount;
    QPushButton *m_browse;
    QDialogButtonBox *m_buttons;
    bool m_animatable = false;
};
