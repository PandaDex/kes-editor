#pragma once
#include "framework/EditorModule.h"
#include "framework/EditorTab.h"
#include "texture_format.h"
#include <QImage>
#include <QList>
#include <functional>
#include <memory>
#include <vector>

class QCheckBox;
class QComboBox;
class QGridLayout;
class QLabel;
class QSpinBox;
class QThread;
class QWidget;

namespace ktx
{
    class TextureView;

    class TextureEditorTab : public EditorTab
    {
        Q_OBJECT
    public:
        explicit TextureEditorTab(EditorContext *ctx, QWidget *parent = nullptr);
        ~TextureEditorTab() override;

        QString filePath() const override { return m_path; }
        bool isModified() const override { return m_modified; }
        bool openFile(const QString &path, QString *error) override;
        bool save() override;
        bool saveAs() override;
        void projectChanged() override;

    private:
        struct SettingRow
        {
            uint32_t bit = 0;
            QCheckBox *override = nullptr;
            QWidget *editor = nullptr;
            std::function<void(const TextureGroupSettings &)> show;
            std::function<void(TextureGroupSettings &)> store;
        };

        void buildDetails();
        void addSettingRow(int row, const QString &label, uint32_t bit, QWidget *editor,
            std::function<void(const TextureGroupSettings &)> show, std::function<void(TextureGroupSettings &)> store);
        void fillGroups();
        void refreshDetails();
        void refreshInfo();
        void refreshPreview(bool keepView);
        void settingsEdited();
        void startCook();
        struct CookJob;
        void applyCook(CookJob &job);
        void finishCooks();
        void reimport();

        TextureView *m_view = nullptr;
        QSpinBox *m_mip = nullptr;
        QLabel *m_mipSize = nullptr;
        QLabel *m_zoom = nullptr;
        QComboBox *m_group = nullptr;
        QGridLayout *m_grid = nullptr;
        QLabel *m_info = nullptr;
        QLabel *m_warning = nullptr;
        QLabel *m_busy = nullptr;
        QList<SettingRow> m_rows;
        bool m_channels[4] = { true, true, true, true };

        QString m_path;
        TextureFileData m_texture;
        QImage m_source;
        std::vector<TextureGroup> m_groups;
        bool m_modified = false;
        bool m_updating = false;

        struct CookJob
        {
            QThread *thread = nullptr;
            int generation = 0;
            TextureFileData result;
            bool ok = false;
            bool applied = false;
            QString error;
        };
        std::vector<std::shared_ptr<CookJob>> m_jobs;
        int m_generation = 0;
    };

    class TextureModule : public EditorModule
    {
    public:
        QString id() const override { return QStringLiteral("texture"); }
        QString name() const override { return QStringLiteral("Texture"); }
        QStringList extensions() const override { return { QStringLiteral("ktex") }; }
        EditorTab *createTab(EditorContext *ctx) const override { return new TextureEditorTab(ctx); }
    };
}
