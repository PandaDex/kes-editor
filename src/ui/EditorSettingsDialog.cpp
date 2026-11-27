#include "ui/EditorSettingsDialog.h"
#include "framework/EditorSettings.h"
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QVBoxLayout>

EditorSettingsDialog::EditorSettingsDialog(QWidget* parent) : QDialog(parent)
{
    setWindowTitle(tr("Editor Settings"));
    auto* layout = new QVBoxLayout(this);
    auto* form = new QFormLayout;
    layout->addLayout(form);

    m_startupMap = new QComboBox(this);
    m_startupMap->addItem(tr("Project Default"), int(editorSettings::StartupMap::ProjectDefault));
    m_startupMap->addItem(tr("Last Opened"), int(editorSettings::StartupMap::LastOpened));
    m_startupMap->setItemData(0, tr("The startup map from the .kproject"), Qt::ToolTipRole);
    m_startupMap->setItemData(1, tr("The map last opened in this project, else the project default"), Qt::ToolTipRole);
    m_startupMap->setCurrentIndex(m_startupMap->findData(int(editorSettings::startupMap())));
    form->addRow(tr("Open Map On Startup"), m_startupMap);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, [this]
        { apply(); accept(); });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

void EditorSettingsDialog::apply()
{
    editorSettings::setStartupMap(editorSettings::StartupMap(m_startupMap->currentData().toInt()));
}
