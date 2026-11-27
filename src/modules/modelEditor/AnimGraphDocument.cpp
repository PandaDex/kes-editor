#include "AnimGraphDocument.h"

#include "GraphModel.h"

#include <QJsonArray>
#include <QRegularExpression>
#include <QSet>
#include <QUndoCommand>
#include <algorithm>

namespace kme
{

    namespace
    {

        constexpr int kMaxNesting = 32;

        class DocumentSnapshotCommand : public QUndoCommand
        {
        public:
            DocumentSnapshotCommand(AnimGraphDocument* doc, QJsonObject before, QJsonObject after, const QString& text, const QString& mergeKey)
                : QUndoCommand(text)
                , m_doc(doc)
                , m_before(std::move(before))
                , m_after(std::move(after))
                , m_mergeKey(mergeKey)
            {
            }

            int id() const override { return m_mergeKey.isEmpty() ? -1 : int(qHash(m_mergeKey) & 0x7fffffff); }

            bool mergeWith(const QUndoCommand* other) override
            {
                auto* o = dynamic_cast<const DocumentSnapshotCommand*>(other);
                if (!o || m_mergeKey.isEmpty() || o->m_mergeKey != m_mergeKey) return false;
                m_after = o->m_after;
                return true;
            }

            void undo() override { m_doc->restore(m_before); }
            void redo() override
            {
                if (m_first)
                {
                    m_first = false;
                    return;
                }
                m_doc->restore(m_after);
            }

        private:
            AnimGraphDocument* m_doc;
            QJsonObject m_before, m_after;
            QString m_mergeKey;
            bool m_first = true;
        };

        bool isParamNode(const QString& type)
        {
            return type == QLatin1String("FloatParam") || type == QLatin1String("BoolParam") || type == QLatin1String("IntParam");
        }

        QString opName(AnimConditionOp op)
        {
            return QString::fromLatin1(AnimConditionOpName(op));
        }

        AnimConditionOp opFromName(const QString& name)
        {
            AnimConditionOp op = AnimConditionOp::IsTrue;
            ParseAnimConditionOp(name.toStdString(), op);
            return op;
        }

        QJsonObject conditionJson(const QString& parameter, AnimConditionOp op, double value)
        {
            return QJsonObject{ { "parameter", parameter }, { "op", opName(op) }, { "value", value } };
        }

        // Condition text in the file is "<parameter> <op> [value]", so names can't hold spaces.
        bool validParameterName(const QString& name)
        {
            return !name.isEmpty() && !name.contains(QRegularExpression(QStringLiteral("\\s")));
        }

    } // namespace

    // ---------------------------------------------------------------------------------------------

    StateMachineModel::StateMachineModel(AnimGraphDocument* doc, GraphModel* owner, int nodeId)
        : QObject(doc)
        , m_doc(doc)
        , m_owner(owner)
        , m_nodeId(nodeId)
    {
    }

    QString StateMachineModel::name() const
    {
        const QString n = m_owner->param(m_nodeId, QStringLiteral("Name")).toString();
        return n.isEmpty() ? QStringLiteral("State Machine") : n;
    }

    const AnimState* StateMachineModel::state(int id) const
    {
        for (const AnimState& s : m_states)
            if (s.id == id) return &s;
        return nullptr;
    }

    QString StateMachineModel::stateName(int id) const
    {
        if (id == AnyState) return QStringLiteral("Any State");
        const AnimState* s = state(id);
        return s ? s->name : QString();
    }

    const AnimTransition* StateMachineModel::transition(int id) const
    {
        for (const AnimTransition& t : m_transitions)
            if (t.id == id) return &t;
        return nullptr;
    }

    QString StateMachineModel::uniqueName(const QString& wanted, int exceptId) const
    {
        const QString base = wanted.trimmed().isEmpty() ? QStringLiteral("State") : wanted.trimmed();
        const auto taken = [&](const QString& n)
        {
            return std::any_of(m_states.begin(), m_states.end(), [&](const AnimState& s)
                { return s.id != exceptId && s.name.compare(n, Qt::CaseInsensitive) == 0; });
        };
        if (!taken(base)) return base;
        for (int i = 2;; ++i)
        {
            const QString candidate = QString("%1 %2").arg(base).arg(i);
            if (!taken(candidate)) return candidate;
        }
    }

    int StateMachineModel::addState(const QPointF& pos, const QString& name)
    {
        AnimState s;
        s.id = m_nextState++;
        s.name = uniqueName(name, 0);
        s.pos = pos;
        s.graph = m_doc->createGraph(this);
        s.graph->addNode(QStringLiteral("Output"), QPointF(300, 0));
        m_states.append(s);
        if (m_entry == 0) m_entry = s.id;
        m_doc->markStructure();
        emit changed();
        return s.id;
    }

    void StateMachineModel::removeState(int id)
    {
        const auto it = std::find_if(m_states.begin(), m_states.end(), [id](const AnimState& s)
            { return s.id == id; });
        if (it == m_states.end()) return;
        GraphModel* graph = it->graph;
        m_states.erase(it);
        m_transitions.erase(std::remove_if(m_transitions.begin(), m_transitions.end(), [id](const AnimTransition& t)
                                { return t.from == id || t.to == id; }),
            m_transitions.end());
        if (m_entry == id) m_entry = m_states.isEmpty() ? 0 : m_states.first().id;
        m_doc->destroyGraph(graph);
        m_doc->markStructure();
        emit changed();
    }

    void StateMachineModel::setStateName(int id, const QString& name)
    {
        for (AnimState& s : m_states)
        {
            if (s.id != id) continue;
            const QString unique = uniqueName(name, id);
            if (s.name == unique) return;
            s.name = unique;
            emit changed();
            return;
        }
    }

    void StateMachineModel::setStatePos(int id, const QPointF& pos)
    {
        for (AnimState& s : m_states)
        {
            if (s.id != id || s.pos == pos) continue;
            s.pos = pos;
            emit changed();
            return;
        }
    }

    void StateMachineModel::setEntryState(int id)
    {
        if (!state(id) || m_entry == id) return;
        m_entry = id;
        emit changed();
    }

    void StateMachineModel::setAnyStatePos(const QPointF& pos)
    {
        if (m_anyPos == pos) return;
        m_anyPos = pos;
        emit changed();
    }

    int StateMachineModel::addTransition(int from, int to)
    {
        if (!state(to) || (from != AnyState && !state(from))) return -1;
        AnimTransition t;
        t.id = m_nextTransition++;
        t.from = from;
        t.to = to;
        m_transitions.append(t);
        emit changed();
        return t.id;
    }

    void StateMachineModel::removeTransition(int id)
    {
        const auto before = m_transitions.size();
        m_transitions.erase(std::remove_if(m_transitions.begin(), m_transitions.end(), [id](const AnimTransition& t)
                                { return t.id == id; }),
            m_transitions.end());
        if (m_transitions.size() != before) emit changed();
    }

    void StateMachineModel::setTransition(const AnimTransition& transition)
    {
        for (AnimTransition& t : m_transitions)
        {
            if (t.id != transition.id) continue;
            t.duration = std::max(0.0, transition.duration);
            t.useExitTime = transition.useExitTime;
            t.exitTime = std::clamp(transition.exitTime, 0.0, 1.0);
            t.conditions = transition.conditions;
            for (AnimCondition& c : t.conditions)
                c.parameter = c.parameter.simplified().replace(' ', '_');
            emit changed();
            return;
        }
    }

    void StateMachineModel::moveTransition(int id, int delta)
    {
        for (int i = 0; i < m_transitions.size(); ++i)
        {
            if (m_transitions[i].id != id) continue;
            const int j = std::clamp(i + delta, 0, int(m_transitions.size()) - 1);
            if (i == j) return;
            m_transitions.move(i, j);
            emit changed();
            return;
        }
    }

    // ---------------------------------------------------------------------------------------------

    AnimGraphDocument::AnimGraphDocument(QObject* parent)
        : QObject(parent)
    {
        m_root = createGraph(this);
    }

    AnimGraphDocument::~AnimGraphDocument()
    {
        blockSignals(true);
        qDeleteAll(m_machines);
        m_machines.clear();
    }

    GraphModel* AnimGraphDocument::createGraph(QObject* parent)
    {
        auto* g = new GraphModel(parent);
        g->setUndoHost(this);
        connect(g, &nodegraph::GraphModel::changed, this, &AnimGraphDocument::changed);
        connect(g, &nodegraph::GraphModel::nodeAdded, this, [this, g](int id)
            { onNodeAdded(g, id); });
        connect(g, &nodegraph::GraphModel::nodeRemoved, this, [this, g](int id)
            { onNodeRemoved(g, id); });
        return g;
    }

    StateMachineModel* AnimGraphDocument::createMachine(GraphModel* owner, int nodeId)
    {
        auto* m = new StateMachineModel(this, owner, nodeId);
        connect(m, &StateMachineModel::changed, this, &AnimGraphDocument::changed);
        m_machines.append(m);
        markStructure();
        return m;
    }

    void AnimGraphDocument::addDefaultState(StateMachineModel* machine, const QPointF& pos, const QString& name)
    {
        machine->addState(pos, name);
    }

    void AnimGraphDocument::removeMachine(StateMachineModel* machine)
    {
        if (!m_machines.contains(machine)) return;
        for (const AnimState& s : machine->m_states)
            destroyGraph(s.graph);
        machine->m_states.clear();
        emit machineAboutToBeRemoved(machine);
        m_machines.removeOne(machine);
        delete machine;
        markStructure();
    }

    void AnimGraphDocument::destroyGraph(GraphModel* graph)
    {
        if (!graph) return;
        for (StateMachineModel* m : machinesIn(graph))
            removeMachine(m);
        emit graphAboutToBeRemoved(graph);
        delete graph;
        markStructure();
    }

    void AnimGraphDocument::markStructure()
    {
        if (m_inEdit || m_restoring)
            m_structureDirty = true;
        else
            emit structureChanged();
    }

    void AnimGraphDocument::onNodeAdded(GraphModel* graph, int id)
    {
        if (m_restoring) return;
        const GraphNode* n = graph->node(id);
        if (!n || n->type != QLatin1String("StateMachine") || machine(graph, id)) return;
        addDefaultState(createMachine(graph, id), QPointF(0, 0), QStringLiteral("Idle"));
    }

    void AnimGraphDocument::onNodeRemoved(GraphModel* graph, int id)
    {
        if (m_restoring) return;
        if (StateMachineModel* m = machine(graph, id)) removeMachine(m);
    }

    void AnimGraphDocument::syncMachines(GraphModel* graph)
    {
        for (int id : graph->nodeIds())
        {
            const GraphNode* n = graph->node(id);
            if (n && n->type == QLatin1String("StateMachine") && !machine(graph, id))
                addDefaultState(createMachine(graph, id), QPointF(0, 0), QStringLiteral("Idle"));
        }
    }

    void AnimGraphDocument::edit(const QString& text, const std::function<void()>& fn, const QString& mergeKey)
    {
        if (m_inEdit || m_restoring)
        {
            fn();
            return;
        }
        const QJsonObject before = snapshot();
        m_inEdit = true;
        fn();
        m_inEdit = false;
        const QJsonObject after = snapshot();
        if (m_structureDirty)
        {
            m_structureDirty = false;
            emit structureChanged();
        }
        if (before == after) return;
        m_undo.push(new DocumentSnapshotCommand(this, before, after, text, mergeKey));
    }

    StateMachineModel* AnimGraphDocument::machine(const GraphModel* graph, int nodeId) const
    {
        for (StateMachineModel* m : m_machines)
            if (m->ownerGraph() == graph && m->nodeId() == nodeId) return m;
        return nullptr;
    }

    QList<StateMachineModel*> AnimGraphDocument::machinesIn(const GraphModel* graph) const
    {
        QList<StateMachineModel*> r;
        for (StateMachineModel* m : m_machines)
            if (m->ownerGraph() == graph) r << m;
        std::sort(r.begin(), r.end(), [](const StateMachineModel* a, const StateMachineModel* b)
            { return a->nodeId() < b->nodeId(); });
        return r;
    }

    StateMachineModel* AnimGraphDocument::ownerOf(const GraphModel* graph, int* stateId) const
    {
        for (StateMachineModel* m : m_machines)
        {
            for (const AnimState& s : m->states())
            {
                if (s.graph != graph) continue;
                if (stateId) *stateId = s.id;
                return m;
            }
        }
        return nullptr;
    }

    QList<GraphModel*> AnimGraphDocument::allGraphs() const
    {
        QList<GraphModel*> r{ m_root };
        for (StateMachineModel* m : m_machines)
            for (const AnimState& s : m->states())
                r << s.graph;
        return r;
    }

    QStringList AnimGraphDocument::parameterNames() const
    {
        QSet<QString> names;
        for (GraphModel* g : allGraphs())
        {
            for (int id : g->nodeIds())
            {
                const GraphNode* n = g->node(id);
                if (n && isParamNode(n->type))
                {
                    const QString name = g->param(id, QStringLiteral("Name")).toString();
                    if (!name.isEmpty()) names.insert(name);
                }
            }
        }
        for (StateMachineModel* m : m_machines)
            for (const AnimTransition& t : m->transitions())
                for (const AnimCondition& c : t.conditions)
                    if (!c.parameter.isEmpty()) names.insert(c.parameter);
        QStringList r(names.begin(), names.end());
        r.sort(Qt::CaseInsensitive);
        return r;
    }

    AnimPortType AnimGraphDocument::parameterType(const QString& name) const
    {
        for (GraphModel* g : allGraphs())
        {
            for (int id : g->nodeIds())
            {
                const GraphNode* n = g->node(id);
                if (!n || !isParamNode(n->type) || g->param(id, QStringLiteral("Name")).toString() != name) continue;
                if (n->type == QLatin1String("BoolParam")) return AnimPortType::Bool;
                if (n->type == QLatin1String("IntParam")) return AnimPortType::Int;
                return AnimPortType::Float;
            }
        }
        return AnimPortType::Float;
    }

    void AnimGraphDocument::newDocument()
    {
        restore(QJsonObject{ { "root", QJsonObject{ { "format", m_root->domain().format() }, { "nodes", QJsonArray() }, { "connections", QJsonArray() } } } });
        m_root->addNode(QStringLiteral("Output"), QPointF(300, 0));
        m_undo.clear();
    }

    // ---------------------------------------------------------------------------------------------
    // Snapshots (undo) use the graphs' own JSON with each machine nested under the graph that holds it.

    QJsonObject AnimGraphDocument::snapshot() const
    {
        return QJsonObject{ { "root", graphSnapshot(m_root) } };
    }

    QJsonObject AnimGraphDocument::graphSnapshot(const GraphModel* graph) const
    {
        QJsonObject o = graph->toJson();
        QJsonArray machines;
        for (StateMachineModel* m : machinesIn(graph))
        {
            QJsonArray states;
            for (const AnimState& s : m->states())
                states.append(QJsonObject{ { "id", s.id }, { "name", s.name }, { "x", s.pos.x() }, { "y", s.pos.y() },
                    { "graph", graphSnapshot(s.graph) } });
            QJsonArray transitions;
            for (const AnimTransition& t : m->transitions())
            {
                QJsonArray conditions;
                for (const AnimCondition& c : t.conditions)
                    conditions.append(conditionJson(c.parameter, c.op, c.value));
                transitions.append(QJsonObject{ { "id", t.id }, { "from", t.from }, { "to", t.to }, { "duration", t.duration },
                    { "useExitTime", t.useExitTime }, { "exitTime", t.exitTime }, { "conditions", conditions } });
            }
            machines.append(QJsonObject{ { "node", m->nodeId() }, { "states", states }, { "transitions", transitions },
                { "entry", m->entryState() }, { "anyX", m->anyStatePos().x() }, { "anyY", m->anyStatePos().y() },
                { "nextState", m->m_nextState }, { "nextTransition", m->m_nextTransition } });
        }
        o["machines"] = machines;
        return o;
    }

    void AnimGraphDocument::restore(const QJsonObject& snapshot)
    {
        const bool outer = !m_restoring;
        m_restoring = true;
        restoreGraph(m_root, snapshot.value("root").toObject());
        if (!outer) return;
        m_restoring = false;
        m_structureDirty = false;
        emit structureChanged();
        emit changed();
    }

    void AnimGraphDocument::restoreGraph(GraphModel* graph, const QJsonObject& json)
    {
        graph->loadJson(json);
        QHash<int, QJsonObject> wanted;
        for (const QJsonValue& v : json.value("machines").toArray())
        {
            const QJsonObject o = v.toObject();
            const int id = o.value("node").toInt(-1);
            const GraphNode* n = graph->node(id);
            if (n && n->type == QLatin1String("StateMachine")) wanted.insert(id, o);
        }
        for (StateMachineModel* m : machinesIn(graph))
            if (!wanted.contains(m->nodeId())) removeMachine(m);
        for (auto it = wanted.constBegin(); it != wanted.constEnd(); ++it)
        {
            StateMachineModel* m = machine(graph, it.key());
            if (!m) m = createMachine(graph, it.key());
            restoreMachine(m, it.value());
        }
        syncMachines(graph);
    }

    void AnimGraphDocument::restoreMachine(StateMachineModel* m, const QJsonObject& json)
    {
        QHash<int, GraphModel*> oldGraphs;
        for (const AnimState& s : m->m_states)
            oldGraphs.insert(s.id, s.graph);

        QVector<AnimState> states;
        QVector<QJsonObject> stateGraphs;
        for (const QJsonValue& v : json.value("states").toArray())
        {
            const QJsonObject o = v.toObject();
            AnimState s;
            s.id = o.value("id").toInt();
            if (s.id <= 0 || std::any_of(states.begin(), states.end(), [&](const AnimState& e)
                                 { return e.id == s.id; }))
                continue;
            s.name = o.value("name").toString();
            s.pos = QPointF(o.value("x").toDouble(), o.value("y").toDouble());
            s.graph = oldGraphs.take(s.id);
            if (!s.graph) s.graph = createGraph(m);
            states.append(s);
            stateGraphs.append(o.value("graph").toObject());
        }
        for (GraphModel* g : std::as_const(oldGraphs))
            destroyGraph(g);

        QVector<AnimTransition> transitions;
        for (const QJsonValue& v : json.value("transitions").toArray())
        {
            const QJsonObject o = v.toObject();
            AnimTransition t;
            t.id = o.value("id").toInt();
            t.from = o.value("from").toInt();
            t.to = o.value("to").toInt();
            t.duration = o.value("duration").toDouble(0.2);
            t.useExitTime = o.value("useExitTime").toBool();
            t.exitTime = o.value("exitTime").toDouble(1.0);
            for (const QJsonValue& c : o.value("conditions").toArray())
            {
                const QJsonObject co = c.toObject();
                t.conditions.append({ co.value("parameter").toString(), opFromName(co.value("op").toString()), co.value("value").toDouble() });
            }
            transitions.append(t);
        }

        int maxState = 0, maxTransition = 0;
        for (const AnimState& s : states)
            maxState = std::max(maxState, s.id);
        for (const AnimTransition& t : transitions)
            maxTransition = std::max(maxTransition, t.id);
        m->m_states = states;
        m->m_transitions = transitions;
        m->m_entry = json.value("entry").toInt();
        if (!m->state(m->m_entry)) m->m_entry = states.isEmpty() ? 0 : states.first().id;
        m->m_anyPos = QPointF(json.value("anyX").toDouble(-260), json.value("anyY").toDouble(-120));
        m->m_nextState = std::max(json.value("nextState").toInt(1), maxState + 1);
        m->m_nextTransition = std::max(json.value("nextTransition").toInt(1), maxTransition + 1);

        for (int i = 0; i < states.size(); ++i)
        {
            QJsonObject g = stateGraphs[i];
            if (g.isEmpty()) g = QJsonObject{ { "format", m_root->domain().format() }, { "nodes", QJsonArray() }, { "connections", QJsonArray() } };
            restoreGraph(states[i].graph, g);
        }
        emit m->changed();
    }

    // ---------------------------------------------------------------------------------------------
    // .ag files: nodes of every graph in one list, a state's nodes tagged with its id.

    AnimBlendTreeData AnimGraphDocument::toBlendTree(AnimGraphIds* ids) const
    {
        AnimBlendTreeData tree;
        tree.modelPath = m_root->modelPath().toStdString();
        if (ids) *ids = {};
        int32_t nextNode = 1, nextState = 1;
        appendGraph(tree, m_root, 0, nextNode, nextState, ids);
        return tree;
    }

    void AnimGraphDocument::appendGraph(AnimBlendTreeData& tree, const GraphModel* graph, int32_t graphId, int32_t& nextNode,
        int32_t& nextState, AnimGraphIds* ids) const
    {
        QHash<int, int> nodes;
        graph->appendToBlendTree(tree, graphId, nextNode, &nodes);
        if (ids) ids->nodes.insert(graph, nodes);

        for (StateMachineModel* m : machinesIn(graph))
        {
            if (!nodes.contains(m->nodeId())) continue;
            const int32_t machineId = nodes.value(m->nodeId());
            for (AnimBlendTreeNode& n : tree.nodes)
                if (n.id == machineId)
                    n.Set("AnyPosition", QString("%1,%2").arg(m->anyStatePos().x()).arg(m->anyStatePos().y()).toStdString());

            QHash<int, int> states;
            for (const AnimState& s : m->states())
                states.insert(s.id, nextState++);
            if (ids) ids->states.insert(m, states);

            for (const AnimState& s : m->states())
            {
                AnimBlendTreeState out;
                out.id = states.value(s.id);
                out.machine = machineId;
                out.name = s.name.toStdString();
                out.position[0] = float(s.pos.x());
                out.position[1] = float(s.pos.y());
                out.entry = s.id == m->entryState();
                tree.states.push_back(std::move(out));
            }
            for (const AnimTransition& t : m->transitions())
            {
                AnimBlendTreeTransition out;
                out.machine = machineId;
                out.from = t.from == StateMachineModel::AnyState ? AnimBlendTreeTransition::AnyState : states.value(t.from);
                out.to = states.value(t.to);
                out.duration = float(t.duration);
                out.exitTime = t.useExitTime ? float(t.exitTime) : -1.0f;
                for (const AnimCondition& c : t.conditions)
                    if (validParameterName(c.parameter))
                        out.conditions.push_back({ c.parameter.toStdString(), c.op, float(c.value) });
                tree.transitions.push_back(std::move(out));
            }
            for (const AnimState& s : m->states())
                appendGraph(tree, s.graph, states.value(s.id), nextNode, nextState, ids);
        }
    }

    void AnimGraphDocument::loadBlendTree(const AnimBlendTreeData& tree, QStringList* warnings)
    {
        restore(QJsonObject{ { "root", graphFromTree(tree, 0, 0, warnings) } });
        m_undo.clear();
    }

    QJsonObject AnimGraphDocument::graphFromTree(const AnimBlendTreeData& tree, int32_t graphId, int depth, QStringList* warnings) const
    {
        QJsonObject o = m_root->graphJson(tree, graphId);
        if (depth > kMaxNesting)
        {
            if (warnings) *warnings << QStringLiteral("State machines are nested too deep; the inner ones were dropped.");
            return o;
        }
        QJsonArray machines;
        for (const AnimBlendTreeNode& n : tree.nodes)
        {
            if (n.graph != graphId || n.type != "StateMachine") continue;
            QJsonObject m{ { "node", n.id } };
            if (const std::string* any = n.Find("AnyPosition"))
            {
                const QStringList xy = QString::fromStdString(*any).split(',');
                if (xy.size() == 2)
                {
                    m["anyX"] = xy[0].toDouble();
                    m["anyY"] = xy[1].toDouble();
                }
            }
            QJsonArray states;
            int entry = 0;
            for (const AnimBlendTreeState& s : tree.states)
            {
                if (s.machine != n.id) continue;
                if (s.entry && entry == 0) entry = s.id;
                states.append(QJsonObject{ { "id", s.id }, { "name", QString::fromStdString(s.name) }, { "x", s.position[0] },
                    { "y", s.position[1] }, { "graph", graphFromTree(tree, s.id, depth + 1, warnings) } });
            }
            QJsonArray transitions;
            int nextTransition = 1;
            for (const AnimBlendTreeTransition& t : tree.transitions)
            {
                if (t.machine != n.id) continue;
                QJsonArray conditions;
                for (const AnimTransitionCondition& c : t.conditions)
                    conditions.append(conditionJson(QString::fromStdString(c.parameter), c.op, c.value));
                transitions.append(QJsonObject{ { "id", nextTransition++ },
                    { "from", t.from == AnimBlendTreeTransition::AnyState ? StateMachineModel::AnyState : t.from }, { "to", t.to },
                    { "duration", t.duration }, { "useExitTime", t.exitTime >= 0.0f }, { "exitTime", t.exitTime >= 0.0f ? t.exitTime : 1.0 },
                    { "conditions", conditions } });
            }
            m["states"] = states;
            m["transitions"] = transitions;
            m["entry"] = entry;
            m["nextTransition"] = nextTransition;
            machines.append(m);
        }
        o["machines"] = machines;
        return o;
    }

} // namespace kme
