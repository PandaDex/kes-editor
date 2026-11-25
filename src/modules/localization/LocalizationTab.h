#pragma once
#include "Dialogs.h"
#include "KesProject.h"
#include "KeyScanner.h"
#include "LocProject.h"
#include "framework/EditorModule.h"
#include "framework/EditorTab.h"

#include <functional>

class QComboBox;
class QLabel;
class QLineEdit;
class QMenu;
class QTableView;
class QToolButton;
class QUndoStack;

namespace kle
{

    class DetailsPanel;
    class MissingKeysPanel;
    class StringFilterModel;
    class StringTableModel;

    // Every .po of a project's content/localization folder in one table, plus the #Keys used in
    // content and C++ that have no text yet. Follows Forge's current project.
    class LocalizationTab : public EditorTab
    {
        Q_OBJECT
    public:
        explicit LocalizationTab(EditorContext* ctx, QWidget* parent = nullptr);

        QString filePath() const override;
        QString displayName() const override;
        bool isModified() const override;
        // a .kproject, a .po or any folder inside a game project
        bool openFile(const QString& path, QString* error) override;
        bool save() override;
        bool saveAs() override { return save(); }
        bool handlesFile(const QString& path) const override;
        void projectChanged() override;

        void setProjectState(const LocProject& state);

    private:
        void buildTable();
        void buildActions();

        bool openProject(const QString& path, QString* error);
        // Edits with the same mergeKey in a row become one undo step, so typing isn't undone per letter.
        void applyEdit(const QString& text, const QString& mergeKey, const std::function<void(LocProject&)>& edit);
        void refreshProject();
        void rescan();

        void updateActions();
        void updateStatus();
        void updateLanguageMenu();
        void applyColumnLayout();
        void setVisibleLanguages(const QStringList& codes);
        void readVisibleFromHeader();

        void loadProjectSettings();
        void saveProjectSettings() const;
        QString settingsGroup() const;

        void reload();
        void addKey(const QString& prefill = {});
        void addKeys(const QStringList& keys);
        void deleteSelectedKeys();
        void addLanguage();
        void editProjectSettings();
        void nextNeedsWork();

        QString currentKey() const;
        void selectKey(const QString& key);

        ProjectPaths paths_;
        ProjectSettings settings_;
        LocProject project_;
        ScanResult scan_;
        bool scanned_ = false;
        bool loaded_ = false;
        QStringList visible_; // in column order
        QStringList knownLanguages_;
        bool applyingLayout_ = false;
        int mergeSession_ = 0;

        QUndoStack* undo_;
        StringTableModel* model_;
        StringFilterModel* filter_;
        QTableView* table_ = nullptr;
        DetailsPanel* details_ = nullptr;
        MissingKeysPanel* missing_ = nullptr;
        QLineEdit* search_ = nullptr;
        QComboBox* filterMode_ = nullptr;
        QToolButton* languagesButton_ = nullptr;
        QMenu* languagesMenu_ = nullptr;
        QLabel* statusLabel_ = nullptr;
        QLabel* progressLabel_ = nullptr;

        QAction* actReload_ = nullptr;
        QAction* actAddKey_ = nullptr;
        QAction* actDeleteKey_ = nullptr;
        QAction* actAddLanguage_ = nullptr;
        QAction* actRescan_ = nullptr;
        QAction* actSettings_ = nullptr;
        QAction* actFind_ = nullptr;
        QAction* actNext_ = nullptr;
    };

    class LocalizationModule : public EditorModule
    {
    public:
        QString id() const override { return QStringLiteral("localization"); }
        QString name() const override { return QStringLiteral("Localization"); }
        QStringList extensions() const override { return { QStringLiteral("po") }; }
        QString projectToolLabel() const override { return QStringLiteral("&Localization"); }
        bool allowsMultipleTabs() const override { return false; }
        EditorTab* createTab(EditorContext* ctx) const override { return new LocalizationTab(ctx); }
    };

} // namespace kle
