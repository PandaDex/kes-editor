#pragma once
#include <QDialog>

class QComboBox;

class EditorSettingsDialog : public QDialog
{
    Q_OBJECT
public:
    explicit EditorSettingsDialog(QWidget* parent = nullptr);

private:
    void apply();

    QComboBox* m_startupMap;
};
