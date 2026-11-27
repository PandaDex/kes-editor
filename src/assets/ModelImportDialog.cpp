#include "assets/ModelImportDialog.h"
#include <QCheckBox>
#include <QComboBox>
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
#include <QSpinBox>
#include <QVBoxLayout>
#include <algorithm>

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
    m_lodMode = new QComboBox;
    m_lodMode->addItem(tr("Keep existing"), -1);
    m_lodMode->addItem(tr("None"), 0);
    m_lodMode->addItem(tr("Generate"), 1);
    m_lodMode->setToolTip(tr("Meshes named <mesh>_LOD<n> in the source are used as LODs instead."));
    m_lodCount = new QSpinBox;
    m_lodCount->setRange(1, 8);
    m_lodCount->setSuffix(tr(" levels"));
    auto *lodRow = new QHBoxLayout;
    lodRow->addWidget(m_lodMode, 1);
    lodRow->addWidget(m_lodCount);
    auto *form = new QFormLayout;
    form->addRow(tr("LODs"), lodRow);
    form->addRow(tr("Target model"), targetRow);
    layout->addLayout(form);

    m_buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    m_buttons->button(QDialogButtonBox::Ok)->setText(tr("Import"));
    layout->addWidget(m_buttons);

    QSettings settings;
    m_mesh->setChecked(!m_animatable || settings.value("import/model/mesh", true).toBool());
    m_animations->setChecked(m_animatable && settings.value("import/model/animations", true).toBool());
    m_target->setText(QDir::toNativeSeparators(settings.value("import/model/target").toString()));
    m_lodMode->setCurrentIndex(std::max(0, m_lodMode->findData(settings.value("import/model/lodMode", -1).toInt())));
    m_lodCount->setValue(settings.value("import/model/lodCount", 3).toInt());

    connect(m_mesh, &QCheckBox::toggled, this, &ModelImportDialog::updateState);
    connect(m_animations, &QCheckBox::toggled, this, &ModelImportDialog::updateState);
    connect(m_browse, &QPushButton::clicked, this, &ModelImportDialog::browseTarget);
    connect(m_lodMode, &QComboBox::currentIndexChanged, this, &ModelImportDialog::updateState);
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
    m_lodMode->setEnabled(m_mesh->isChecked());
    m_lodCount->setEnabled(m_mesh->isChecked() && m_lodMode->currentData().toInt() > 0);
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
    const int mode = m_lodMode->currentData().toInt();
    c.lodCount = mode > 0 ? m_lodCount->value() : mode;
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
    settings.setValue("import/model/lodMode", dialog.m_lodMode->currentData().toInt());
    settings.setValue("import/model/lodCount", dialog.m_lodCount->value());
    return true;
}
