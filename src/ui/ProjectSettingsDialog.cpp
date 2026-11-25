#include "ui/ProjectSettingsDialog.h"
#include "core/EditorContext.h"
#include "core/MapDocument.h"
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QRegularExpressionValidator>

namespace
{
    QWidget *withButtons(QLineEdit *edit, std::initializer_list<QPushButton *> buttons, QWidget *parent)
    {
        auto *row = new QWidget(parent);
        auto *h = new QHBoxLayout(row);
        h->setContentsMargins(0, 0, 0, 0);
        h->addWidget(edit, 1);
        for (QPushButton *b : buttons)
            h->addWidget(b);
        return row;
    }

    bool isInside(const QString &path, const QString &dir)
    {
        const QString rel = QDir(dir).relativeFilePath(path);
        return !rel.startsWith("..") && !QDir::isAbsolutePath(rel);
    }
}

ProjectSettingsDialog::ProjectSettingsDialog(EditorContext *ctx, QWidget *parent)
    : QDialog(parent), m_ctx(ctx)
{
    const KesProject &p = ctx->project();
    setWindowTitle(tr("Project Settings"));
    setMinimumWidth(520);

    auto *form = new QFormLayout(this);

    auto *file = new QLabel(QDir::toNativeSeparators(p.filePath), this);
    file->setObjectName("hint");
    file->setTextInteractionFlags(Qt::TextSelectableByMouse);
    form->addRow(tr("Project file"), file);

    m_name = new QLineEdit(p.name, this);
    m_name->setToolTip(tr("Window title, user data folder and package names"));
    form->addRow(tr("&Name"), m_name);

    m_company = new QLineEdit(p.company, this);
    m_company->setPlaceholderText("KES");
    m_company->setToolTip(tr("Publisher: user data folder and GDK publisher name"));
    form->addRow(tr("&Company"), m_company);

    m_version = new QLineEdit(p.version, this);
    m_version->setPlaceholderText("1.0.0.0");
    m_version->setToolTip(tr("Four numbers, major.minor.build.revision"));
    m_version->setValidator(new QRegularExpressionValidator(QRegularExpression(R"(\d{0,5}(\.\d{0,5}){0,3})"), m_version));
    form->addRow(tr("&Version"), m_version);

    m_assetRoot = new QLineEdit(p.assetRootValue, this);
    m_assetRoot->setToolTip(tr("Content folder, relative to the .kproject folder"));
    auto *browseRoot = new QPushButton(tr("Browse..."), this);
    connect(browseRoot, &QPushButton::clicked, this, &ProjectSettingsDialog::browseAssetRoot);
    form->addRow(tr("&Asset root"), withButtons(m_assetRoot, { browseRoot }, this));

    m_startupMap = new QLineEdit(p.startupMap, this);
    m_startupMap->setToolTip(tr("Compiled map the game loads first, relative to the asset root"));
    auto *browseMap = new QPushButton(tr("Browse..."), this);
    connect(browseMap, &QPushButton::clicked, this, &ProjectSettingsDialog::browseStartupMap);
    m_currentMap = new QPushButton(tr("Current Map"), this);
    m_currentMap->setToolTip(tr("Use the map open in the editor"));
    connect(m_currentMap, &QPushButton::clicked, this, &ProjectSettingsDialog::useCurrentMap);
    form->addRow(tr("&Startup map"), withButtons(m_startupMap, { browseMap, m_currentMap }, this));

    m_entityDefs = new QLineEdit(p.entityDefPathsValue == QLatin1String("editor") ? QString() : p.entityDefPathsValue, this);
    m_entityDefs->setPlaceholderText(tr("editor"));
    m_entityDefs->setToolTip(tr("Folders with the project's .dec entity definitions, relative to the .kproject folder, separated by ';'. "
                                "Empty uses \"editor\". They load after Forge's own entities and can override them."));
    auto *addDefs = new QPushButton(tr("Add Folder..."), this);
    connect(addDefs, &QPushButton::clicked, this, &ProjectSettingsDialog::addEntityDefFolder);
    form->addRow(tr("&Entity definitions"), withButtons(m_entityDefs, { addDefs }, this));

    m_status = new QLabel(this);
    m_status->setObjectName("hint");
    m_status->setWordWrap(true);
    form->addRow(m_status);

    m_buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
    form->addRow(m_buttons);
    connect(m_buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(m_buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    for (QLineEdit *e : { m_name, m_company, m_version, m_assetRoot, m_startupMap, m_entityDefs })
        connect(e, &QLineEdit::textChanged, this, &ProjectSettingsDialog::updateState);
    updateState();
}

QString ProjectSettingsDialog::projectDir() const
{
    return QFileInfo(m_ctx->project().filePath).absolutePath();
}

QString ProjectSettingsDialog::contentRoot() const
{
    return QDir::cleanPath(QDir(projectDir()).absoluteFilePath(m_assetRoot->text().trimmed()));
}

// Returns the error that blocks saving ("" = ok); *warning gets a non-blocking note.
QString ProjectSettingsDialog::validate(QString *warning) const
{
    static const QRegularExpression badName(R"([\"\\/:*?<>|])");
    static const QRegularExpression version(R"(^\d+\.\d+\.\d+\.\d+$)");

    const QString name = m_name->text().trimmed();
    if (name.isEmpty())
        return tr("Name is required.");
    if (name.contains(badName))
        return tr("Name is used in file names and can't contain \" \\ / : * ? < > |");
    // Paths get their backslashes converted on save; the .kproject format has no escapes.
    if (m_company->text().contains('\\'))
        return tr("Company can't contain backslashes.");
    for (QLineEdit *e : { m_company, m_assetRoot, m_startupMap, m_entityDefs })
        if (e->text().contains('"'))
            return tr("Values can't contain quotes.");
    const QString ver = m_version->text().trimmed();
    if (!ver.isEmpty() && !version.match(ver).hasMatch())
        return tr("Version must be four numbers, like 1.0.0.0.");
    if (m_assetRoot->text().trimmed().isEmpty())
        return tr("Asset root is required.");
    if (!QFileInfo(contentRoot()).isDir())
        return tr("Asset root folder %1 does not exist.").arg(QDir::toNativeSeparators(contentRoot()));

    const QString map = m_startupMap->text().trimmed();
    if (map.isEmpty())
        *warning = tr("No startup map: the game starts without loading a map.");
    else if (QDir::isAbsolutePath(map))
        return tr("Startup map must be relative to the asset root.");
    else if (!map.endsWith(".fmapc", Qt::CaseInsensitive))
        *warning = tr("The engine loads compiled maps (.fmapc).");
    else if (!QFileInfo::exists(QDir(contentRoot()).filePath(map)))
        *warning = tr("%1 is not built yet: open the map and use Map > Build Map (F9).").arg(map);
    return {};
}

void ProjectSettingsDialog::updateState()
{
    QString warning;
    const QString error = validate(&warning);
    m_status->setText(error.isEmpty() ? warning : error);
    m_status->setStyleSheet(error.isEmpty() ? QString() : QStringLiteral("color: #e05050;"));
    m_buttons->button(QDialogButtonBox::Save)->setEnabled(error.isEmpty());

    const QString current = m_ctx->doc()->filePath();
    m_currentMap->setEnabled(!current.isEmpty() && !toStartupMap(current).isEmpty());
}

void ProjectSettingsDialog::browseAssetRoot()
{
    const QString start = QFileInfo(contentRoot()).isDir() ? contentRoot() : projectDir();
    const QString dir = QFileDialog::getExistingDirectory(this, tr("Asset Root"), start);
    if (dir.isEmpty())
        return;
    QString rel = QDir(projectDir()).relativeFilePath(dir);
    m_assetRoot->setText(rel.isEmpty() ? QStringLiteral(".") : rel);
}

QString ProjectSettingsDialog::toStartupMap(const QString &absolutePath) const
{
    const QFileInfo info(absolutePath);
    QString compiled = info.absoluteFilePath();
    if (info.suffix().compare("fmap", Qt::CaseInsensitive) == 0)
        compiled = info.dir().filePath(info.completeBaseName() + ".fmapc");
    if (!isInside(compiled, contentRoot()))
        return {};
    return QDir(contentRoot()).relativeFilePath(compiled);
}

void ProjectSettingsDialog::browseStartupMap()
{
    QString start = QDir(contentRoot()).filePath(m_startupMap->text().trimmed());
    if (m_startupMap->text().trimmed().isEmpty() || !QFileInfo::exists(start))
        start = QDir(contentRoot()).exists("maps") ? QDir(contentRoot()).filePath("maps") : contentRoot();
    const QString file = QFileDialog::getOpenFileName(this, tr("Startup Map"), start,
        tr("Maps (*.fmapc *.fmap);;Compiled Maps (*.fmapc);;Forge Maps (*.fmap)"));
    if (file.isEmpty())
        return;
    const QString map = toStartupMap(file);
    if (map.isEmpty())
    {
        QMessageBox::warning(this, tr("Startup Map"),
            tr("%1 is outside the asset root %2.").arg(QDir::toNativeSeparators(file), QDir::toNativeSeparators(contentRoot())));
        return;
    }
    m_startupMap->setText(map);
}

void ProjectSettingsDialog::useCurrentMap()
{
    const QString map = toStartupMap(m_ctx->doc()->filePath());
    if (!map.isEmpty())
        m_startupMap->setText(map);
}

void ProjectSettingsDialog::addEntityDefFolder()
{
    const QString dir = QFileDialog::getExistingDirectory(this, tr("Entity Definitions Folder"), projectDir());
    if (dir.isEmpty())
        return;
    const QString rel = QDir(projectDir()).relativeFilePath(dir);
    QStringList dirs = m_entityDefs->text().split(';', Qt::SkipEmptyParts);
    if (dirs.isEmpty())
        dirs << QStringLiteral("editor");
    if (!dirs.contains(rel))
        dirs << rel;
    m_entityDefs->setText(dirs.join(';'));
}

void ProjectSettingsDialog::accept()
{
    QString warning;
    if (const QString error = validate(&warning); !error.isEmpty())
        return;

    const QString path = m_ctx->project().filePath;
    const QList<QPair<QString, QString>> values = {
        { "Name", m_name->text().trimmed() },
        { "Company", m_company->text().trimmed() },
        { "Version", m_version->text().trimmed() },
        { "AssetRoot", QDir::fromNativeSeparators(m_assetRoot->text().trimmed()) },
        { "StartupMap", QDir::fromNativeSeparators(m_startupMap->text().trimmed()) },
        // the default isn't written; an empty value removes the key
        { "EntityDefPaths", m_entityDefs->text().trimmed() == QLatin1String("editor") ? QString() : QDir::fromNativeSeparators(m_entityDefs->text().trimmed()) },
    };
    QString error;
    if (!KesProject::writeValues(path, values, &error))
    {
        QMessageBox::critical(this, tr("Project Settings"), error);
        return;
    }
    if (!m_ctx->loadProject(path, &error))
    {
        QMessageBox::critical(this, tr("Project Settings"), error);
        return;
    }
    m_ctx->log(tr("Saved project settings to %1").arg(QDir::toNativeSeparators(path)));
    QDialog::accept();
}
