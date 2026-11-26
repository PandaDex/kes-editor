#include "ui/BrandDialogs.h"
#include "core/ProjectFile.h"

#include <QDialogButtonBox>
#include <QDir>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPixmap>
#include <QPushButton>
#include <QSettings>
#include <QVBoxLayout>

#ifndef FORGE_VERSION
#define FORGE_VERSION "dev"
#endif

QPixmap kesLogo(int height, qreal dpr)
{
    QPixmap logo(QStringLiteral(":/brand/logo-light.png"));
    if (logo.isNull())
        return {};
    QPixmap scaled = logo.scaledToHeight(int(height * dpr), Qt::SmoothTransformation);
    scaled.setDevicePixelRatio(dpr);
    return scaled;
}

namespace
{
    QLabel* logoLabel(QWidget* parent, int height)
    {
        auto* label = new QLabel(parent);
        label->setPixmap(kesLogo(height, parent->devicePixelRatioF()));
        return label;
    }

    QLabel* versionLabel(QWidget* parent)
    {
        auto* label = new QLabel(QStringLiteral("KES Editor %1").arg(QStringLiteral(FORGE_VERSION)), parent);
        label->setObjectName("Muted");
        return label;
    }
}

WelcomeDialog::WelcomeDialog(QWidget* parent) : QDialog(parent)
{
    setWindowTitle(tr("KES Editor"));
    setMinimumSize(560, 420);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(28, 28, 28, 20);
    layout->setSpacing(14);

    auto* header = new QHBoxLayout;
    header->addWidget(logoLabel(this, 34));
    header->addStretch();
    header->addWidget(versionLabel(this), 0, Qt::AlignBottom);
    layout->addLayout(header);
    layout->addSpacing(10);

    auto* heading = new QLabel(tr("Recent Projects"), this);
    heading->setObjectName("Heading");
    layout->addWidget(heading);

    m_recent = new QListWidget(this);
    m_recent->setObjectName("RecentProjects");
    m_recent->setStyleSheet(QStringLiteral("QListWidget#RecentProjects { border-radius: 8px; padding: 4px; }"
                                           "QListWidget#RecentProjects::item { padding: 8px 10px; border-radius: 6px; }"));
    QStringList recent = QSettings().value("recentProjects").toStringList();
    if (const QString last = QSettings().value("project").toString(); !last.isEmpty() && !recent.contains(last))
        recent.prepend(last);
    for (const QString& path : recent)
    {
        if (!QFileInfo::exists(path))
            continue;
        KesProject project;
        const QString name = KesProject::load(path, project) && !project.name.isEmpty() ? project.name : QFileInfo(path).completeBaseName();
        auto* item = new QListWidgetItem(QStringLiteral("%1\n%2").arg(name, QDir::toNativeSeparators(path)), m_recent);
        item->setData(Qt::UserRole, path);
        item->setToolTip(QDir::toNativeSeparators(path));
    }
    if (m_recent->count() > 0)
    {
        m_recent->setCurrentRow(0);
        layout->addWidget(m_recent, 1);
    }
    else
    {
        m_recent->hide();
        auto* empty = new QLabel(tr("No recent projects."), this);
        empty->setObjectName("EmptyState");
        empty->setAlignment(Qt::AlignCenter);
        layout->addWidget(empty, 1);
    }
    connect(m_recent, &QListWidget::itemActivated, this, [this](QListWidgetItem* item)
        { choose(item->data(Qt::UserRole).toString()); });

    auto* buttons = new QHBoxLayout;
    auto* skip = new QPushButton(tr("Continue Without Project"), this);
    skip->setAutoDefault(false);
    connect(skip, &QPushButton::clicked, this, &QDialog::reject);
    auto* browse = new QPushButton(tr("Open Project..."), this);
    browse->setAutoDefault(false);
    connect(browse, &QPushButton::clicked, this, [this]
        { choose(QString()); });
    buttons->addWidget(skip);
    buttons->addStretch();
    buttons->addWidget(browse);
    if (m_recent->count() > 0)
    {
        auto* open = new QPushButton(tr("Open"), this);
        open->setDefault(true);
        connect(open, &QPushButton::clicked, this, [this]
            {
            if (QListWidgetItem* item = m_recent->currentItem()) choose(item->data(Qt::UserRole).toString()); });
        buttons->addWidget(open);
    }
    else
    {
        browse->setDefault(true);
    }
    layout->addLayout(buttons);
}

void WelcomeDialog::choose(const QString& path)
{
    m_chosen = path;
    accept();
}

void showAbout(QWidget* parent)
{
    QDialog dialog(parent);
    dialog.setWindowTitle(QObject::tr("About KES Editor"));
    auto* layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(28, 28, 28, 20);
    layout->setSpacing(12);
    layout->addWidget(logoLabel(&dialog, 40));
    layout->addSpacing(6);
    auto* title = new QLabel(QObject::tr("KES Editor"), &dialog);
    title->setObjectName("SectionTitle");
    layout->addWidget(title);
    layout->addWidget(versionLabel(&dialog));
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);
    dialog.exec();
}
