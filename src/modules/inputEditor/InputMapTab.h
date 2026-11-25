#pragma once
#include "framework/EditorModule.h"
#include "framework/EditorTab.h"

class QDockWidget;
class QLabel;

namespace imap
{
    class ActionDetailsPanel;
    class ActionListPanel;
    class InputDocument;
    class PreviewPanel;
    class ProblemsPanel;

    // .imap: the game's input actions and their key/gamepad bindings.
    class InputMapTab : public EditorTab
    {
        Q_OBJECT
    public:
        explicit InputMapTab(EditorContext* ctx, QWidget* parent = nullptr);
        ~InputMapTab() override;

        QString filePath() const override;
        QString displayName() const override;
        bool isModified() const override;
        bool openFile(const QString& path, QString* error) override;
        bool save() override;
        bool saveAs() override;

    private:
        void buildActions();
        void onDocumentChanged();
        bool writeTo(const QString& path);

        InputDocument* doc_;
        ActionListPanel* list_;
        ActionDetailsPanel* details_;
        PreviewPanel* preview_;
        ProblemsPanel* problems_;
        QDockWidget* problemsDock_;
        QLabel* statusLabel_;
        QLabel* issueLabel_;
    };

    class InputMapModule : public EditorModule
    {
    public:
        QString id() const override { return QStringLiteral("inputMap"); }
        QString name() const override { return QStringLiteral("Input Map"); }
        QStringList extensions() const override { return { QStringLiteral("imap") }; }
        QString newLabel() const override { return QStringLiteral("Input Map"); }
        EditorTab* createTab(EditorContext* ctx) const override { return new InputMapTab(ctx); }
    };
}
