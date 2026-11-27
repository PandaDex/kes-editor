#pragma once

#include "framework/EditorModule.h"
#include "framework/EditorTab.h"

#include <QHash>
#include <QPointer>
#include <QStringList>
#include <QVector>
#include <functional>

class QAction;
class QLabel;
class QSplitter;
class QStackedWidget;

namespace nodegraph
{
    class GraphEditor;
}

namespace kme
{

    class AnimGraphDocument;
    class AnimationPanel;
    class AttachmentPanel;
    class StateMachineEditor;
    class StateMachineModel;
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

    // .ag: animation blend tree with nested state machines, previewed live on the model the graph names.
    class AnimGraphTab : public EditorTab
    {
        Q_OBJECT
    public:
        explicit AnimGraphTab(EditorContext* ctx, QWidget* parent = nullptr);
        ~AnimGraphTab() override;

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

        // One level of the breadcrumb: a graph, or a state machine.
        struct Layer
        {
            QPointer<GraphModel> graph;
            QPointer<StateMachineModel> machine;
        };
        nodegraph::GraphEditor* graphEditor(GraphModel* graph);
        StateMachineEditor* machineEditor(StateMachineModel* machine);
        void dropGraphEditor(GraphModel* graph);
        void dropMachineEditor(StateMachineModel* machine);
        void openMachine(GraphModel* graph, int nodeId);
        void openState(StateMachineModel* machine, int stateId);
        void goToDepth(int depth);
        void validatePath();
        void showCurrentLayer();
        void rebuildBreadcrumbs();
        void updateWeights();
        nodegraph::GraphEditor* currentGraphEditor() const;
        StateMachineEditor* currentMachineEditor() const;

        ModelDocument* m_doc;
        AnimGraphDocument* m_graphDoc;
        GraphModel* m_graph;
        PreviewState* m_preview;
        QHash<GraphModel*, nodegraph::GraphEditor*> m_editors;
        QHash<StateMachineModel*, StateMachineEditor*> m_machineEditors;
        QHash<QObject*, QWidget*> m_inspectorScrolls;
        QVector<Layer> m_path;
        QWidget* m_breadcrumbs = nullptr;
        QStackedWidget* m_canvas = nullptr;
        QStackedWidget* m_inspectors = nullptr;
        QStackedWidget* m_libraries = nullptr;
        QWidget* m_emptyLibrary = nullptr;
        QList<QAction*> m_graphOnlyActions;
        QAction* m_snap = nullptr;
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
