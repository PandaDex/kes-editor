#include "modules/texture/TextureEditorTab.h"
#include "core/EditorContext.h"
#include "core/TextureFile.h"
#include "modules/texture/TextureView.h"
#include "ui/KesStyle.h"
#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QDockWidget>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QLabel>
#include <QMenu>
#include <QScrollArea>
#include <QSpinBox>
#include <QThread>
#include <QToolBar>
#include <QVBoxLayout>
#include <algorithm>

namespace ktx
{
    namespace
    {
        QString bytesText(uint64_t bytes)
        {
            if (bytes >= 1024ull * 1024)
                return QString::number(double(bytes) / (1024.0 * 1024.0), 'f', 2) + " MiB";
            if (bytes >= 1024)
                return QString::number(double(bytes) / 1024.0, 'f', 1) + " KiB";
            return QString::number(bytes) + " B";
        }

        QComboBox *enumCombo(const QList<QPair<QString, int>> &items)
        {
            auto *combo = new QComboBox;
            for (const auto &[text, value] : items)
                combo->addItem(text, value);
            return combo;
        }

        void selectData(QComboBox *combo, int value)
        {
            const int index = combo->findData(value);
            combo->setCurrentIndex(index >= 0 ? index : 0);
        }

        bool isDataCompression(TextureCompression c)
        {
            return c == TextureCompression::NormalMap || c == TextureCompression::Masks || c == TextureCompression::Grayscale;
        }

        QLabel *sectionTitle(const QString &text)
        {
            auto *label = new QLabel(text);
            QFont font = label->font();
            font.setBold(true);
            label->setFont(font);
            return label;
        }
    }

    TextureEditorTab::TextureEditorTab(EditorContext *ctx, QWidget *parent)
        : EditorTab(ctx, QStringLiteral("texture"), parent)
    {
        m_groups = TextureFile::projectGroups(ctx->project());

        m_view = new TextureView;
        setCentralWidget(m_view);

        QToolBar *bar = addToolBar(tr("Preview"));
        bar->setObjectName("TexturePreviewBar");
        const char *channelNames[] = { "R", "G", "B", "A" };
        const char *channelTips[] = { "Red channel", "Green channel", "Blue channel", "Alpha channel" };
        for (int c = 0; c < 4; ++c)
        {
            QAction *a = bar->addAction(channelNames[c]);
            a->setCheckable(true);
            a->setChecked(true);
            a->setToolTip(tr(channelTips[c]));
            connect(a, &QAction::toggled, this, [this, c](bool on)
                    {
                        m_channels[c] = on;
                        m_view->setChannels(m_channels[0], m_channels[1], m_channels[2], m_channels[3]);
                    });
        }
        bar->addSeparator();
        bar->addWidget(new QLabel(tr("Mip ")));
        m_mip = new QSpinBox;
        m_mip->setRange(0, 0);
        m_mip->setToolTip(tr("Mip level shown"));
        connect(m_mip, &QSpinBox::valueChanged, this, [this] { refreshPreview(true); });
        bar->addWidget(m_mip);
        m_mipSize = new QLabel;
        m_mipSize->setObjectName("Muted");
        m_mipSize->setContentsMargins(6, 0, 6, 0);
        bar->addWidget(m_mipSize);
        bar->addSeparator();
        QAction *fit = bar->addAction(tr("Fit"));
        fit->setToolTip(tr("Fit the texture in the view (double-click)"));
        connect(fit, &QAction::triggered, m_view, &TextureView::fit);
        QAction *actual = bar->addAction(tr("100%"));
        connect(actual, &QAction::triggered, m_view, &TextureView::actualSize);
        m_zoom = new QLabel;
        m_zoom->setObjectName("Muted");
        m_zoom->setContentsMargins(6, 0, 6, 0);
        bar->addWidget(m_zoom);
        connect(m_view, &TextureView::zoomChanged, this, [this](double zoom)
                { m_zoom->setText(QString::number(qRound(zoom * 100.0)) + "%"); });
        m_busy = new QLabel(tr("Compressing..."));
        m_busy->setObjectName("Muted");
        m_busy->setContentsMargins(12, 0, 6, 0);
        m_busy->hide();
        bar->addWidget(m_busy);

        buildDetails();

        QMenu *texture = addTabMenu(tr("&Texture"));
        QAction *reimportAction = texture->addAction(tr("Reimport..."));
        reimportAction->setToolTip(tr("Replace the source image and cook it with the current settings"));
        connect(reimportAction, &QAction::triggered, this, &TextureEditorTab::reimport);

        QMenu *view = addTabMenu(tr("&View"));
        for (QDockWidget *dock : findChildren<QDockWidget *>())
            view->addAction(dock->toggleViewAction());
    }

    TextureEditorTab::~TextureEditorTab()
    {
        for (const std::shared_ptr<CookJob> &job : m_jobs)
        {
            job->thread->wait();
            delete job->thread;
        }
    }

    void TextureEditorTab::buildDetails()
    {
        auto *panel = new QWidget;
        auto *layout = new QVBoxLayout(panel);
        layout->setContentsMargins(10, 10, 10, 10);
        layout->setSpacing(8);

        layout->addWidget(sectionTitle(tr("Info")));
        m_info = new QLabel;
        m_info->setTextFormat(Qt::RichText);
        m_info->setTextInteractionFlags(Qt::TextSelectableByMouse);
        layout->addWidget(m_info);
        m_warning = new QLabel;
        m_warning->setWordWrap(true);
        m_warning->setStyleSheet(QString("color: %1;").arg(kes::style::warning().name()));
        m_warning->hide();
        layout->addWidget(m_warning);

        layout->addSpacing(6);
        layout->addWidget(sectionTitle(tr("Compression")));
        auto *grid = m_grid = new QGridLayout;
        grid->setHorizontalSpacing(8);
        grid->setVerticalSpacing(6);
        grid->setColumnStretch(2, 1);
        layout->addLayout(grid);

        m_group = new QComboBox;
        m_group->setToolTip(tr("Texture group: every setting not overridden below comes from it"));
        grid->addWidget(new QLabel(tr("Group")), 0, 1);
        grid->addWidget(m_group, 0, 2);
        connect(m_group, &QComboBox::currentIndexChanged, this, [this]
                {
                    if (m_updating || m_group->currentIndex() < 0)
                        return;
                    m_texture.settings.group = m_group->currentData().toString().toStdString();
                    settingsEdited();
                });
        fillGroups();

        int row = 1;
        auto *compression = enumCombo({ { tr("Default (BC7)"), int(TextureCompression::Default) },
                                        { tr("Default Small (BC1)"), int(TextureCompression::DefaultSmall) },
                                        { tr("Normal Map (BC5)"), int(TextureCompression::NormalMap) },
                                        { tr("Masks (BC7, linear)"), int(TextureCompression::Masks) },
                                        { tr("Grayscale (BC4)"), int(TextureCompression::Grayscale) },
                                        { tr("User Interface (RGBA8)"), int(TextureCompression::UserInterface) },
                                        { tr("Uncompressed (RGBA8)"), int(TextureCompression::Uncompressed) } });
        addSettingRow(row++, tr("Compression"), TextureSetting_Compression, compression,
            [compression](const TextureGroupSettings &s) { selectData(compression, int(s.compression)); },
            [compression](TextureGroupSettings &s) { s.compression = TextureCompression(compression->currentData().toInt()); });

        auto *srgb = new QCheckBox;
        addSettingRow(row++, tr("sRGB"), TextureSetting_Srgb, srgb,
            [srgb](const TextureGroupSettings &s) { srgb->setChecked(s.srgb); },
            [srgb](TextureGroupSettings &s) { s.srgb = srgb->isChecked(); });

        grid->addWidget(sectionTitle(tr("Mipmaps and Size")), row++, 0, 1, 3);

        auto *mipGen = enumCombo({ { tr("Box"), int(TextureMipGen::Box) }, { tr("No Mipmaps"), int(TextureMipGen::NoMipmaps) } });
        addSettingRow(row++, tr("Mip Gen"), TextureSetting_MipGen, mipGen,
            [mipGen](const TextureGroupSettings &s) { selectData(mipGen, int(s.mipGen)); },
            [mipGen](TextureGroupSettings &s) { s.mipGen = TextureMipGen(mipGen->currentData().toInt()); });

        QList<QPair<QString, int>> sizes = { { tr("No Limit"), 0 } };
        for (int size = 32; size <= 8192; size *= 2)
            sizes.append({ QString::number(size), size });
        auto *maxSize = enumCombo(sizes);
        addSettingRow(row++, tr("Max Size"), TextureSetting_MaxSize, maxSize,
            [maxSize](const TextureGroupSettings &s) { selectData(maxSize, int(s.maxSize)); },
            [maxSize](TextureGroupSettings &s) { s.maxSize = uint32_t(maxSize->currentData().toInt()); });

        auto *lodBias = new QSpinBox;
        lodBias->setRange(0, 12);
        lodBias->setKeyboardTracking(false);
        lodBias->setToolTip(tr("Top mips dropped when cooking"));
        addSettingRow(row++, tr("LOD Bias"), TextureSetting_LodBias, lodBias,
            [lodBias](const TextureGroupSettings &s) { lodBias->setValue(s.lodBias); },
            [lodBias](TextureGroupSettings &s) { s.lodBias = lodBias->value(); });

        grid->addWidget(sectionTitle(tr("Sampling")), row++, 0, 1, 3);
        auto *filter = enumCombo({ { tr("Default (Anisotropic)"), int(TextureFilter::Default) },
                                   { tr("Nearest"), int(TextureFilter::Nearest) },
                                   { tr("Bilinear"), int(TextureFilter::Bilinear) },
                                   { tr("Trilinear"), int(TextureFilter::Trilinear) } });
        addSettingRow(row++, tr("Filter"), TextureSetting_Filter, filter,
            [filter](const TextureGroupSettings &s) { selectData(filter, int(s.filter)); },
            [filter](TextureGroupSettings &s) { s.filter = TextureFilter(filter->currentData().toInt()); });

        const QList<QPair<QString, int>> address = { { tr("Wrap"), int(TextureAddress::Wrap) },
                                                     { tr("Clamp"), int(TextureAddress::Clamp) },
                                                     { tr("Mirror"), int(TextureAddress::Mirror) } };
        auto *addressU = enumCombo(address);
        addSettingRow(row++, tr("Address X"), TextureSetting_AddressU, addressU,
            [addressU](const TextureGroupSettings &s) { selectData(addressU, int(s.addressU)); },
            [addressU](TextureGroupSettings &s) { s.addressU = TextureAddress(addressU->currentData().toInt()); });
        auto *addressV = enumCombo(address);
        addSettingRow(row++, tr("Address Y"), TextureSetting_AddressV, addressV,
            [addressV](const TextureGroupSettings &s) { selectData(addressV, int(s.addressV)); },
            [addressV](TextureGroupSettings &s) { s.addressV = TextureAddress(addressV->currentData().toInt()); });

        layout->addStretch(1);

        auto *scroll = new QScrollArea;
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setWidget(panel);
        auto *dock = new QDockWidget(tr("Details"), this);
        dock->setObjectName("TextureDetailsDock");
        dock->setWidget(scroll);
        dock->setMinimumWidth(300);
        addDockWidget(Qt::RightDockWidgetArea, dock);
    }

    void TextureEditorTab::addSettingRow(int row, const QString &label, uint32_t bit, QWidget *editor,
        std::function<void(const TextureGroupSettings &)> show, std::function<void(TextureGroupSettings &)> store)
    {
        QGridLayout *grid = m_grid;
        SettingRow r;
        r.bit = bit;
        r.override = new QCheckBox;
        r.override->setToolTip(tr("Override the group's value"));
        r.editor = editor;
        r.show = std::move(show);
        r.store = std::move(store);
        grid->addWidget(r.override, row, 0);
        grid->addWidget(new QLabel(label), row, 1);
        grid->addWidget(editor, row, 2);

        connect(r.override, &QCheckBox::toggled, this, [this, bit](bool on)
                {
                    if (m_updating)
                        return;
                    if (on)
                    {
                        for (const SettingRow &row : m_rows)
                            if (row.bit == bit)
                                row.store(m_texture.settings.values);
                        m_texture.settings.overrides |= bit;
                    }
                    else
                    {
                        m_texture.settings.overrides &= ~bit;
                    }
                    settingsEdited();
                });

        auto edited = [this, bit]
        {
            if (m_updating)
                return;
            for (const SettingRow &row : m_rows)
                if (row.bit == bit)
                    row.store(m_texture.settings.values);
            m_texture.settings.overrides |= bit;
            settingsEdited();
        };
        if (auto *combo = qobject_cast<QComboBox *>(editor))
            connect(combo, &QComboBox::currentIndexChanged, this, edited);
        else if (auto *check = qobject_cast<QCheckBox *>(editor))
            connect(check, &QCheckBox::toggled, this, edited);
        else if (auto *spin = qobject_cast<QSpinBox *>(editor))
            connect(spin, &QSpinBox::valueChanged, this, edited);
        m_rows.append(r);
    }

    void TextureEditorTab::fillGroups()
    {
        const bool wasUpdating = m_updating;
        m_updating = true;
        m_group->clear();
        for (const TextureGroup &group : m_groups)
            m_group->addItem(QString::fromStdString(group.name), QString::fromStdString(group.name));
        const QString current = QString::fromStdString(m_texture.settings.group);
        int index = -1;
        for (int i = 0; i < m_group->count(); ++i)
            if (m_group->itemData(i).toString().compare(current, Qt::CaseInsensitive) == 0)
                index = i;
        if (index < 0 && !current.isEmpty())
        {
            m_group->addItem(tr("%1 (missing)").arg(current), current);
            index = m_group->count() - 1;
        }
        m_group->setCurrentIndex(index);
        m_updating = wasUpdating;
    }

    void TextureEditorTab::refreshDetails()
    {
        m_updating = true;
        fillGroups();
        const TextureGroupSettings effective = ResolveTextureSettings(m_texture.settings, m_groups);
        for (const SettingRow &row : m_rows)
        {
            row.override->setChecked((m_texture.settings.overrides & row.bit) != 0);
            row.show(effective);
            if (row.bit == TextureSetting_Srgb)
            {
                const bool forced = isDataCompression(effective.compression);
                row.editor->setEnabled(!forced);
                row.override->setEnabled(!forced);
                row.editor->setToolTip(forced ? tr("Normal map, mask and grayscale compression are always linear") : QString());
            }
        }
        m_updating = false;
    }

    void TextureEditorTab::refreshInfo()
    {
        const TextureFileData &t = m_texture;
        const QString muted = kes::style::muted().name();
        auto row = [&](const QString &key, const QString &value)
        { return QString("<tr><td style='color:%1; padding-right:12px;'>%2</td><td>%3</td></tr>").arg(muted, key, value.toHtmlEscaped()); };

        QString html = "<table cellspacing='0' cellpadding='1'>";
        if (!m_source.isNull())
            html += row(tr("Source"), QString("%1  %2 x %3  %4")
                                          .arg(QString::fromStdString(t.sourcePath))
                                          .arg(m_source.width())
                                          .arg(m_source.height())
                                          .arg(t.sourceData.empty() ? QString() : bytesText(t.sourceData.size())));
        html += row(tr("Cooked"), QString("%1 x %2  %3  %4 mip%5")
                                      .arg(t.width)
                                      .arg(t.height)
                                      .arg(TexturePixelFormatName(t.format))
                                      .arg(t.mipCount)
                                      .arg(t.mipCount == 1 ? "" : "s"));
        html += row(tr("GPU memory"), bytesText(TextureMipOffset(t, t.mipCount)));
        if (!m_path.isEmpty())
            html += row(tr("File"), bytesText(uint64_t(QFileInfo(m_path).size())));
        html += "</table>";
        m_info->setText(html);
    }

    void TextureEditorTab::refreshPreview(bool keepView)
    {
        const uint32_t mip = uint32_t(std::min(m_mip->value(), int(m_texture.mipCount) - 1));
        m_view->setDisplaySize(QSize(int(m_texture.width), int(m_texture.height)));
        m_view->setImage(TextureFile::mipImage(m_texture, mip), keepView);
        m_mipSize->setText(QString("%1 x %2").arg(TextureMipWidth(m_texture.width, mip)).arg(TextureMipWidth(m_texture.height, mip)));
    }

    bool TextureEditorTab::openFile(const QString &path, QString *error)
    {
        TextureFileData texture;
        if (!TextureFile::read(path, texture, error, true))
            return false;
        QString sourceError;
        QImage source = TextureFile::sourceImage(texture, &sourceError);

        m_path = QFileInfo(path).absoluteFilePath();
        m_texture = std::move(texture);
        m_source = source;
        m_modified = false;

        m_updating = true;
        m_mip->setRange(0, int(m_texture.mipCount) - 1);
        m_mip->setValue(0);
        m_updating = false;
        refreshDetails();
        refreshInfo();
        refreshPreview(false);

        if (m_source.isNull())
        {
            m_warning->setText(tr("No source image (%1). Use Texture > Reimport to add one.").arg(sourceError));
            m_warning->show();
        }
        else if (m_texture.version < 2)
        {
            // Old uncompressed texture: cook it right away, so Save converts it.
            m_warning->setText(tr("Uncompressed version 1 texture. Save to convert it."));
            m_warning->show();
            m_modified = true;
            startCook();
        }
        else
        {
            m_warning->hide();
        }
        emit titleChanged();
        return true;
    }

    void TextureEditorTab::settingsEdited()
    {
        m_modified = true;
        emit titleChanged();
        refreshDetails();
        startCook();
    }

    void TextureEditorTab::startCook()
    {
        if (m_source.isNull())
            return;
        auto job = std::make_shared<CookJob>();
        job->generation = ++m_generation;
        TextureFileData base = m_texture;
        base.pixels.clear();
        const QImage source = m_source;
        const std::vector<TextureGroup> groups = m_groups;
        job->thread = QThread::create([job, base, source, groups]() mutable
                                      {
                                          job->result = std::move(base);
                                          job->ok = TextureFile::cook(job->result, source, groups, &job->error);
                                      });
        connect(job->thread, &QThread::finished, this, [this, job]
                {
                    applyCook(*job);
                    job->thread->deleteLater();
                    m_jobs.erase(std::remove(m_jobs.begin(), m_jobs.end(), job), m_jobs.end());
                });
        m_jobs.push_back(job);
        m_busy->show();
        job->thread->start();
    }

    // Only the newest cook's result is used; older ones were superseded by later edits.
    void TextureEditorTab::applyCook(CookJob &job)
    {
        if (job.applied || job.generation != m_generation)
            return;
        job.applied = true;
        m_busy->hide();
        if (!job.ok)
        {
            context()->log(tr("Cooking %1 failed: %2").arg(QFileInfo(m_path).fileName(), job.error), LogLevel::Error);
            return;
        }
        const uint32_t mip = uint32_t(m_mip->value());
        m_texture.version = job.result.version;
        m_texture.width = job.result.width;
        m_texture.height = job.result.height;
        m_texture.format = job.result.format;
        m_texture.flags = job.result.flags;
        m_texture.mipCount = job.result.mipCount;
        m_texture.pixels = std::move(job.result.pixels);
        if (m_texture.sourceData.empty())
        {
            // A version 1 texture: its pixels, saved as PNG, are now the source.
            m_texture.sourceData = std::move(job.result.sourceData);
            m_texture.sourcePath = job.result.sourcePath;
        }
        m_updating = true;
        m_mip->setRange(0, int(m_texture.mipCount) - 1);
        m_mip->setValue(int(std::min(mip, m_texture.mipCount - 1)));
        m_updating = false;
        refreshInfo();
        refreshPreview(true);
    }

    // Save must write the newest cook.
    void TextureEditorTab::finishCooks()
    {
        for (const std::shared_ptr<CookJob> &job : m_jobs)
        {
            job->thread->wait();
            job->thread->disconnect(this);
            job->thread->deleteLater();
            applyCook(*job);
        }
        m_jobs.clear();
    }

    bool TextureEditorTab::save()
    {
        if (m_path.isEmpty())
            return saveAs();
        finishCooks();
        if (m_texture.version < 2)
        {
            showStatus(tr("Nothing to save: the texture has no source image."));
            return false;
        }
        QString error;
        if (!TextureFile::write(m_path, m_texture, &error))
        {
            context()->log(tr("Saving %1 failed: %2").arg(QFileInfo(m_path).fileName(), error), LogLevel::Error);
            return false;
        }
        m_modified = false;
        m_warning->hide();
        refreshInfo();
        emit titleChanged();
        emit fileSaved(m_path);
        context()->materials()->reload();
        showStatus(tr("Saved %1").arg(QFileInfo(m_path).fileName()));
        return true;
    }

    bool TextureEditorTab::saveAs()
    {
        const QString path = QFileDialog::getSaveFileName(this, tr("Save Texture As"), m_path, tr("Textures (*.ktex)"));
        if (path.isEmpty())
            return false;
        m_path = path;
        return save();
    }

    void TextureEditorTab::projectChanged()
    {
        m_groups = TextureFile::projectGroups(context()->project());
        refreshDetails();
    }

    void TextureEditorTab::reimport()
    {
        const QString file = QFileDialog::getOpenFileName(this, tr("Reimport Texture Source"), QFileInfo(m_path).absolutePath(),
            tr("Images (*.png *.jpg *.jpeg *.bmp *.tif *.tiff *.tga)"));
        if (file.isEmpty())
            return;
        QFile in(file);
        if (!in.open(QIODevice::ReadOnly))
        {
            context()->log(tr("Can't read %1: %2").arg(file, in.errorString()), LogLevel::Error);
            return;
        }
        const QByteArray bytes = in.readAll();
        QImage image = QImage::fromData(bytes);
        if (image.isNull())
        {
            context()->log(tr("Can't decode %1").arg(QFileInfo(file).fileName()), LogLevel::Error);
            return;
        }
        m_texture.sourceData.assign(bytes.begin(), bytes.end());
        m_texture.sourcePath = QFileInfo(file).fileName().toStdString();
        m_source = image.convertToFormat(QImage::Format_RGBA8888);
        m_warning->hide();
        settingsEdited();
    }
}
