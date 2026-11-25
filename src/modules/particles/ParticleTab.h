#pragma once

#include "framework/EditorModule.h"
#include "framework/EditorTab.h"

class QLabel;
class QTreeWidget;
class QTreeWidgetItem;
class QScrollArea;
class QComboBox;
class QSplitter;
class QStackedWidget;

namespace nodegraph
{
    class GraphEditor;
    class GraphModel;
}

struct ParticleModuleData;
struct ParticleSystemData;

namespace kpe
{
    class ParticleDocument;
    class ParticleViewport;

    // .kfx particle systems, edited Niagara style: a stack of modules per emitter
    // stage, renderers, and node graphs for any parameter. Live preview on the left.
    class ParticleTab : public EditorTab
    {
        Q_OBJECT
    public:
        explicit ParticleTab(EditorContext* ctx, QWidget* parent = nullptr);
        ~ParticleTab() override;

        QString filePath() const override;
        bool isModified() const override;
        bool openFile(const QString& path, QString* error) override;
        bool save() override;
        bool saveAs() override;
        void projectChanged() override;
        void assetSaved(const QString& path) override;

    private:
        enum class Kind
        {
            None,
            System,
            Emitter, // emitter properties
            Stage, // index = ParticleStage, or 3 for renderers
            Module,
            Renderer
        };
        struct Selection
        {
            Kind kind = Kind::None;
            int emitter = -1;
            int index = -1;
            bool operator==(const Selection& o) const { return kind == o.kind && emitter == o.emitter && index == o.index; }
        };

        void buildActions();
        void rebuildTree();
        void rebuildDetails();
        void onDocumentChanged(bool structural);
        void select(const Selection& s);
        Selection selectionOf(QTreeWidgetItem* item) const;
        Selection currentSelection() const;
        void showContextMenu(const QPoint& pos);
        void addModule(int emitter, int stage, const QString& type);
        void addEmitter(const QString& templateName);
        void removeSelected();
        void moveSelected(int direction);
        void duplicateSelected();
        void openGraph(int emitter, int graphId, const QString& label);
        // Opens the first graph-driven value of the selected module or renderer, if any.
        void openSelectionGraph();
        void closeGraph();
        void reloadGraph();
        void updateContentRoot(const QString& hint);
        QString resolve(const QString& contentPath) const;
        bool saveTo(const QString& path);

        QWidget* systemDetails();
        QWidget* emitterDetails(int emitter);
        QWidget* moduleDetails(int emitter, int index, bool renderer);

        ParticleDocument* m_doc;
        ParticleViewport* m_view = nullptr;
        QTreeWidget* m_tree = nullptr;
        QScrollArea* m_details = nullptr;
        nodegraph::GraphModel* m_graph = nullptr;
        nodegraph::GraphEditor* m_graphEditor = nullptr;
        QLabel* m_graphLabel = nullptr;
        QSplitter* m_centralSplit = nullptr;
        QStackedWidget* m_graphStack = nullptr;
        QLabel* m_stats = nullptr;
        QLabel* m_errors = nullptr;
        QString m_contentRoot;
        Selection m_selection;
        int m_graphEmitter = -1;
        int m_graphId = 0;
        bool m_loadingGraph = false;
        bool m_rebuildingTree = false;
    };

    class ParticleModule : public EditorModule
    {
    public:
        QString id() const override { return QStringLiteral("particles"); }
        QString name() const override { return QStringLiteral("Particle System"); }
        QStringList extensions() const override { return { QStringLiteral("kfx") }; }
        QString newLabel() const override { return QStringLiteral("Particle System"); }
        EditorTab* createTab(EditorContext* ctx) const override { return new ParticleTab(ctx); }
    };
}
