#include "assets/ModelImportDialog.h"
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSettings>
#include <QVBoxLayout>

ModelImportDialog::ModelImportDialog(const QStringList &files, const QString &projectDir, QWidget *parent)
    : QDialog(parent), m_projectDir(projectDir)
{
    setWindowTitle(tr("Import Models"));
    resize(480, 0);

    QStringList names;
    for (const QString &file : files)
    {
        names << QFileInfo(file).fileName();
        if (QFileInfo(file).suffix().compare("obj", Qt::CaseInsensitive) != 0)
            m_animatable = true;
    }
    const int shown = 6;
    QString list = names.mid(0, shown).join('\n');
    if (names.size() > shown)
        list += '\n' + tr("and %1 more").arg(names.size() - shown);

    auto *layout = new QVBoxLayout(this);
    auto *filesLabel = new QLabel(list);
    filesLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(filesLabel);

    m_mesh = new QCheckBox(tr("Mesh and skeleton (.kmodel)"));
    m_animations = new QCheckBox(tr("Animations (.kanim)"));
    layout->addWidget(m_mesh);
    layout->addWidget(m_animations);
    if (!m_animatable)
        m_animations->setToolTip(tr("OBJ files have no animations."));

    m_target = new QLineEdit;
    m_target->setPlaceholderText(tr("Name clips after the source file"));
    m_target->setToolTip(tr("Model the clips are for. Tracks are matched to its nodes by name,\n"
                            "so animations exported without the mesh still line up."));
    m_browse = new QPushButton(tr("Browse..."));
    auto *targetRow = new QHBoxLayout;
    targetRow->addWidget(m_target, 1);
    targetRow->addWidget(m_browse);
    auto *form = new QFormLayout;
    form->addRow(tr("Target model"), targetRow);
    layout->addLayout(form);

    m_buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    m_buttons->button(QDialogButtonBox::Ok)->setText(tr("Import"));
    layout->addWidget(m_buttons);

    QSettings settings;
    m_mesh->setChecked(!m_animatable || settings.value("import/model/mesh", true).toBool());
    m_animations->setChecked(m_animatable && settings.value("import/model/animations", true).toBool());
    m_target->setText(QDir::toNativeSeparators(settings.value("import/model/target").toString()));

    connect(m_mesh, &QCheckBox::toggled, this, &ModelImportDialog::updateState);
    connect(m_animations, &QCheckBox::toggled, this, &ModelImportDialog::updateState);
    connect(m_browse, &QPushButton::clicked, this, &ModelImportDialog::browseTarget);
    connect(m_buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(m_buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    updateState();
}

void ModelImportDialog::updateState()
{
    m_animations->setEnabled(m_animatable);
    const bool animationsOnly = !m_mesh->isChecked() && m_animations->isChecked();
    m_target->setEnabled(animationsOnly);
    m_browse->setEnabled(animationsOnly);
    m_buttons->button(QDialogButtonBox::Ok)->setEnabled(m_mesh->isChecked() || m_animations->isChecked());
}

void ModelImportDialog::browseTarget()
{
    QString start = m_target->text().trimmed();
    if (start.isEmpty())
        start = m_projectDir;
    const QString file = QFileDialog::getOpenFileName(this, tr("Target Model"), start, tr("Models (*.kmodel)"));
    if (!file.isEmpty())
        m_target->setText(QDir::toNativeSeparators(file));
}

ModelImportChoice ModelImportDialog::choice() const
{
    ModelImportChoice c;
    c.mesh = m_mesh->isChecked();
    c.animations = m_animations->isChecked();
    if (c.animations && !c.mesh)
        c.targetModel = QDir::fromNativeSeparators(m_target->text().trimmed());
    return c;
}

bool ModelImportDialog::ask(const QStringList &files, const QString &projectDir, QWidget *parent, ModelImportChoice &out)
{
    ModelImportDialog dialog(files, projectDir, parent);
    if (dialog.exec() != QDialog::Accepted)
        return false;
    out = dialog.choice();

    QSettings settings;
    settings.setValue("import/model/mesh", out.mesh);
    if (dialog.m_animatable)
        settings.setValue("import/model/animations", out.animations);
    settings.setValue("import/model/target", QDir::fromNativeSeparators(dialog.m_target->text().trimmed()));
    return true;
}
