#pragma once

#include "framework/EditorModule.h"
#include "framework/EditorTab.h"

#include <QStringList>
#include <functional>

class QLabel;
class QSplitter;

namespace nodegraph
{
    class GraphEditor;
}

namespace kme
{

    class AnimationPanel;
    class AttachmentPanel;
    class GraphModel;
    class GraphParamsPanel;
    class ModelDocument;
    class ModelViewport;
    class PreviewState;
    class SkeletonPanel;

    // .kmodel: sockets, colliders, child models and material slots, previewed with clips.
    class ModelEditorTab : public EditorTab
    {
        Q_OBJECT
    public:
        explicit ModelEditorTab(EditorContext* ctx, QWidget* parent = nullptr);

        QString filePath() const override;
        bool isModified() const override;
        bool openFile(const QString& path, QString* error) override;
        bool save() override;
        bool saveAs() override;
        void projectChanged() override;
        void assetSaved(const QString& path) override;

    private:
        void buildActions();
        void updateContentRoot();
        void rescanClips();

        ModelDocument* m_doc;
        PreviewState* m_preview;
        ModelViewport* m_view = nullptr;
        SkeletonPanel* m_skeleton = nullptr;
        AttachmentPanel* m_attachments = nullptr;
        AnimationPanel* m_animation = nullptr;
        QLabel* m_statusContent = nullptr;
    };

    // .ag: animation blend tree, previewed live on the model the graph names.
    class AnimGraphTab : public EditorTab
    {
        Q_OBJECT
    public:
        explicit AnimGraphTab(EditorContext* ctx, QWidget* parent = nullptr);

        QString filePath() const override { return m_graphPath; }
        QString displayName() const override;
        bool isModified() const override;
        bool openFile(const QString& path, QString* error) override;
        bool save() override;
        bool saveAs() override;
        void projectChanged() override;
        void assetSaved(const QString& path) override;

    private:
        void buildActions();
        void newGraph();
        void loadPreviewModel();
        void choosePreviewModel();
        void rescanClips();
        QWidget* clipEditor(int nodeId, const QString& current, const std::function<void(const QVariant&)>& apply);

        ModelDocument* m_doc;
        GraphModel* m_graph;
        PreviewState* m_preview;
        nodegraph::GraphEditor* m_editor;
        ModelViewport* m_view = nullptr;
        GraphParamsPanel* m_params = nullptr;
        AnimationPanel* m_animation = nullptr;
        QLabel* m_statusModel = nullptr;
        QString m_graphPath;
        QStringList m_clips;
    };

    class ModelModule : public EditorModule
    {
    public:
        QString id() const override { return QStringLiteral("model"); }
        QString name() const override { return QStringLiteral("Model"); }
        QStringList extensions() const override { return { QStringLiteral("kmodel") }; }
        EditorTab* createTab(EditorContext* ctx) const override { return new ModelEditorTab(ctx); }
    };

    class AnimGraphModule : public EditorModule
    {
    public:
        QString id() const override { return QStringLiteral("animGraph"); }
        QString name() const override { return QStringLiteral("Animation Graph"); }
        QStringList extensions() const override { return { QStringLiteral("ag") }; }
        QString newLabel() const override { return QStringLiteral("Animation Graph"); }
        EditorTab* createTab(EditorContext* ctx) const override { return new AnimGraphTab(ctx); }
    };

} // namespace kme
