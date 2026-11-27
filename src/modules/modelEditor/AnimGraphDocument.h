#pragma once

#include "anim_blend_tree.h"
#include "framework/graph/GraphModel.h"

#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QPointF>
#include <QUndoStack>
#include <QVector>
#include <functional>

namespace kme
{

    class AnimGraphDocument;
    class GraphModel;

    struct AnimCondition
    {
        QString parameter;
        AnimConditionOp op = AnimConditionOp::IsTrue;
        double value = 0.0;
    };

    struct AnimTransition
    {
        int id = 0;
        int from = 0; // StateMachineModel::AnyState for Any State
        int to = 0;
        double duration = 0.2;
        bool useExitTime = false;
        double exitTime = 1.0;
        QVector<AnimCondition> conditions;
    };

    struct AnimState
    {
        int id = 0;
        QString name;
        QPointF pos;
        GraphModel* graph = nullptr;
    };

    // States and transitions of one StateMachine node. Each state owns the graph that makes its pose.
    // Change it only inside AnimGraphDocument::edit() so the change can be undone.
    class StateMachineModel : public QObject
    {
        Q_OBJECT
    public:
        static constexpr int AnyState = -1;

        StateMachineModel(AnimGraphDocument* doc, GraphModel* owner, int nodeId);

        AnimGraphDocument* document() const { return m_doc; }
        GraphModel* ownerGraph() const { return m_owner; }
        int nodeId() const { return m_nodeId; }
        QString name() const;

        const QVector<AnimState>& states() const { return m_states; }
        const AnimState* state(int id) const;
        QString stateName(int id) const;
        const QVector<AnimTransition>& transitions() const { return m_transitions; }
        const AnimTransition* transition(int id) const;
        int entryState() const { return m_entry; }
        QPointF anyStatePos() const { return m_anyPos; }

        int addState(const QPointF& pos, const QString& name = QString());
        void removeState(int id);
        void setStateName(int id, const QString& name);
        void setStatePos(int id, const QPointF& pos);
        void setEntryState(int id);
        void setAnyStatePos(const QPointF& pos);
        int addTransition(int from, int to);
        void removeTransition(int id);
        void setTransition(const AnimTransition& transition);
        void moveTransition(int id, int delta);

    signals:
        void changed();

    private:
        friend class AnimGraphDocument;
        QString uniqueName(const QString& wanted, int exceptId) const;

        AnimGraphDocument* m_doc;
        GraphModel* m_owner;
        int m_nodeId;
        QVector<AnimState> m_states;
        QVector<AnimTransition> m_transitions;
        int m_entry = 0;
        QPointF m_anyPos{ -260, -120 };
        int m_nextState = 1;
        int m_nextTransition = 1;
    };

    // File ids each editor id got in the last toBlendTree().
    struct AnimGraphIds
    {
        QHash<const GraphModel*, QHash<int, int>> nodes;
        QHash<const StateMachineModel*, QHash<int, int>> states;
    };

    // A whole .ag: the top-level graph, its state machines and their state graphs, with one undo stack.
    class AnimGraphDocument : public QObject, public nodegraph::GraphUndoHost
    {
        Q_OBJECT
    public:
        explicit AnimGraphDocument(QObject* parent = nullptr);
        ~AnimGraphDocument() override;

        GraphModel* root() const { return m_root; }
        QUndoStack* undoStack() override { return &m_undo; }
        void edit(const QString& text, const std::function<void()>& fn, const QString& mergeKey = QString()) override;

        StateMachineModel* machine(const GraphModel* graph, int nodeId) const;
        QList<StateMachineModel*> machinesIn(const GraphModel* graph) const;
        // The machine and state whose graph this is; null for the top-level graph.
        StateMachineModel* ownerOf(const GraphModel* graph, int* stateId = nullptr) const;

        // Names used by parameter nodes and transition conditions anywhere in the document.
        QStringList parameterNames() const;
        // Float, Int or Bool when a parameter node declares the name, else Float.
        AnimPortType parameterType(const QString& name) const;

        void newDocument();
        AnimBlendTreeData toBlendTree(AnimGraphIds* ids = nullptr) const;
        void loadBlendTree(const AnimBlendTreeData& tree, QStringList* warnings = nullptr);

        QJsonObject snapshot() const;
        void restore(const QJsonObject& snapshot);

    signals:
        void changed();
        // State machines or state graphs were added or removed.
        void structureChanged();
        void graphAboutToBeRemoved(GraphModel* graph);
        void machineAboutToBeRemoved(StateMachineModel* machine);

    private:
        friend class StateMachineModel;

        GraphModel* createGraph(QObject* parent);
        StateMachineModel* createMachine(GraphModel* owner, int nodeId);
        void addDefaultState(StateMachineModel* machine, const QPointF& pos, const QString& name);
        void removeMachine(StateMachineModel* machine);
        void destroyGraph(GraphModel* graph);
        void onNodeAdded(GraphModel* graph, int id);
        void onNodeRemoved(GraphModel* graph, int id);
        void syncMachines(GraphModel* graph);

        QJsonObject graphSnapshot(const GraphModel* graph) const;
        void restoreGraph(GraphModel* graph, const QJsonObject& json);
        void restoreMachine(StateMachineModel* machine, const QJsonObject& json);
        void appendGraph(AnimBlendTreeData& tree, const GraphModel* graph, int32_t graphId, int32_t& nextNode, int32_t& nextState,
            AnimGraphIds* ids) const;
        QJsonObject graphFromTree(const AnimBlendTreeData& tree, int32_t graphId, int depth, QStringList* warnings) const;
        QList<GraphModel*> allGraphs() const;
        void markStructure();

        GraphModel* m_root;
        QList<StateMachineModel*> m_machines;
        QUndoStack m_undo;
        bool m_inEdit = false;
        bool m_restoring = false;
        bool m_structureDirty = false;
    };

} // namespace kme
