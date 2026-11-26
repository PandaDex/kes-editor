#pragma once
#include <QDialog>
#include <QString>

class QListWidget;

// Start screen shown when the editor opens without a project: logo, recent projects, open or continue.
class WelcomeDialog : public QDialog
{
    Q_OBJECT
public:
    explicit WelcomeDialog(QWidget* parent = nullptr);

    // Accepted: the .kproject to open, or empty to pick one with a file dialog.
    QString chosenProject() const { return m_chosen; }

private:
    void choose(const QString& path);

    QListWidget* m_recent = nullptr;
    QString m_chosen;
};

void showAbout(QWidget* parent);

// The KES logo (icon + wordmark) for dark backgrounds, `height` logical pixels tall.
QPixmap kesLogo(int height, qreal devicePixelRatio);
