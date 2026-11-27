#include "GraphModel.h"

#include "anim_blend_tree.h"

#include <QJsonArray>
#include <QSet>

namespace kme
{

    namespace
    {

        PortType toPortType(AnimPortType t)
        {
            switch (t)
            {
            case AnimPortType::Pose: return PortType::Pose;
            case AnimPortType::Int:  return PortType::Int;
            case AnimPortType::Bool: return PortType::Bool;
            default:                 return PortType::Float;
            }
        }

        ParamKind toParamKind(AnimParamKind k)
        {
            switch (k)
            {
            case AnimParamKind::Int:  return ParamKind::Int;
            case AnimParamKind::Bool: return ParamKind::Bool;
            case AnimParamKind::Enum: return ParamKind::Enum;
            case AnimParamKind::Name: return ParamKind::Identifier;
            case AnimParamKind::Clip: return ParamKind::Clip;
            case AnimParamKind::Bone: return ParamKind::Bone;
            default:                  return ParamKind::Float;
            }
        }

        QVariant defaultFor(const AnimNodeParamInfo& p)
        {
            const QString text = QString::fromStdString(p.defaultValue);
            switch (p.kind)
            {
            case AnimParamKind::Float: return text.toDouble();
            case AnimParamKind::Int:   return text.toInt();
            case AnimParamKind::Bool:  return text == QLatin1String("true") || text == QLatin1String("1");
            default:                   return text;
            }
        }

        QString valueText(const QVariant& v)
        {
            if (v.typeId() == QMetaType::Bool) return v.toBool() ? QStringLiteral("true") : QStringLiteral("false");
            if (v.typeId() == QMetaType::Double || v.typeId() == QMetaType::Float) return QString::number(v.toDouble(), 'g', 9);
            return v.toString();
        }

    } // namespace

    const AnimGraphDomain& AnimGraphDomain::instance()
    {
        static const AnimGraphDomain domain;
        return domain;
    }

    AnimGraphDomain::AnimGraphDomain()
    {
        for (const AnimNodeInfo& info : GetAnimNodeCatalog())
        {
            NodeDef& d = m_catalog.add(QString::fromStdString(info.type), QString::fromStdString(info.title),
                QString::fromStdString(info.category), QString::fromStdString(info.description));
            d.role = info.type == "Output" ? NodeRole::Output : NodeRole::Normal;
            for (const AnimNodePortInfo& p : info.inputs)
                d.in(QString::fromStdString(p.name), toPortType(p.type), { double(p.defaultValue) });
            for (const AnimNodePortInfo& p : info.outputs)
                d.out(QString::fromStdString(p.name), toPortType(p.type));
            for (const AnimNodeParamInfo& p : info.params)
            {
                ParamDef pd;
                pd.name = QString::fromStdString(p.name);
                pd.kind = toParamKind(p.kind);
                pd.defaultValue = defaultFor(p);
                pd.minValue = -1e6;
                pd.maxValue = 1e6;
                for (const std::string& o : p.options)
                    pd.options << QString::fromStdString(o);
                d.params.append(pd);
            }
        }
    }

    bool AnimGraphDomain::typesCompatible(PortType from, PortType to, QString* why) const
    {
        // numbers convert into each other, poses only connect to poses
        if ((from == PortType::Pose) == (to == PortType::Pose)) return true;
        if (why) *why = QStringLiteral("A pose only connects to a pose input");
        return false;
    }

    QString AnimGraphDomain::inputSummary(const PortDef& port, const QVariantList& value, PortType resolved) const
    {
        if (port.type == PortType::Pose) return QStringLiteral("bind pose");
        return nodegraph::GraphDomain::inputSummary(port, value.isEmpty() ? port.defaultValue : value, resolved);
    }

    QString AnimGraphDomain::inspectorHint() const
    {
        return QStringLiteral("Orange ports carry poses; the others carry values.");
    }

    GraphModel::GraphModel(QObject* parent)
        : nodegraph::GraphModel(&AnimGraphDomain::instance(), parent)
    {
        connect(this, &nodegraph::GraphModel::documentChanged, this, &GraphModel::modelPathChanged);
    }

    void GraphModel::setModelPath(const QString& path)
    {
        if (m_modelPath == path) return;
        m_modelPath = path;
        emit modelPathChanged();
        emit changed();
    }

    void GraphModel::writeDocument(QJsonObject& root) const
    {
        root["model"] = m_modelPath;
    }

    void GraphModel::readDocument(const QJsonObject& root)
    {
        m_modelPath = root.value("model").toString();
    }

    void GraphModel::clearDocument()
    {
        m_modelPath.clear();
    }

    AnimBlendTreeData GraphModel::toBlendTree() const
    {
        AnimBlendTreeData tree;
        tree.modelPath = m_modelPath.toStdString();
        int32_t next = 1;
        appendToBlendTree(tree, 0, next, nullptr);
        return tree;
    }

    void GraphModel::appendToBlendTree(AnimBlendTreeData& tree, int32_t graphId, int32_t& nextNodeId, QHash<int, int>* localToFile) const
    {
        QHash<int, int> ids;
        for (int id : nodeIds())
            if (def(id)) ids.insert(id, nextNodeId++);
        if (localToFile) *localToFile = ids;

        for (int id : nodeIds())
        {
            const GraphNode& n = *node(id);
            const NodeDef* d = def(id);
            if (!d) continue;
            AnimBlendTreeNode out;
            out.id = ids.value(id);
            out.graph = graphId;
            out.type = n.type.toStdString();
            out.position[0] = float(n.pos.x());
            out.position[1] = float(n.pos.y());
            for (const ParamDef& p : d->params)
                out.Set(p.name.toStdString(), valueText(param(id, p.name)).toStdString());
            for (int i = 0; i < d->inputs.size(); ++i)
            {
                const PortDef& port = d->inputs[i];
                if (port.type == PortType::Pose || inputConnection(id, i)) continue;
                const QVariantList v = inputDefault(id, i);
                const double value = v.isEmpty() ? port.defaultValue.value(0).toDouble() : v[0].toDouble();
                if (port.type == PortType::Bool)
                    out.Set(port.name.toStdString(), value != 0.0 ? "true" : "false");
                else
                    out.Set(port.name.toStdString(), QString::number(value, 'g', 9).toStdString());
            }
            tree.nodes.push_back(std::move(out));
        }
        for (const GraphConnection& c : connections())
        {
            const NodeDef* df = def(c.fromNode);
            const NodeDef* dt = def(c.toNode);
            if (!df || !dt) continue;
            tree.links.push_back({ ids.value(c.fromNode), df->outputs.value(c.fromPort).name.toStdString(), ids.value(c.toNode),
                dt->inputs.value(c.toPort).name.toStdString() });
        }
    }

    void GraphModel::loadBlendTree(const AnimBlendTreeData& tree, QStringList* warnings)
    {
        loadJson(graphJson(tree, 0), warnings);
    }

    QJsonObject GraphModel::graphJson(const AnimBlendTreeData& tree, int32_t graphId) const
    {
        QJsonArray nodes;
        QSet<int32_t> inGraph;
        for (const AnimBlendTreeNode& n : tree.nodes)
        {
            if (n.graph != graphId) continue;
            inGraph.insert(n.id);
            const QString type = QString::fromStdString(n.type);
            const NodeDef* d = catalog().find(type);
            QJsonObject o;
            o["id"] = n.id;
            o["type"] = type;
            o["x"] = n.position[0];
            o["y"] = n.position[1];
            QJsonObject params, defaults;
            for (const auto& [key, raw] : n.values)
            {
                const QString k = QString::fromStdString(key);
                const QString text = QString::fromStdString(raw);
                if (!d) continue;
                if (const ParamDef* p = d->paramDef(k))
                {
                    switch (p->kind)
                    {
                    case ParamKind::Float: params[k] = text.toDouble(); break;
                    case ParamKind::Int:   params[k] = text.toInt(); break;
                    case ParamKind::Bool:  params[k] = text == QLatin1String("true") || text == QLatin1String("1"); break;
                    default:               params[k] = text; break;
                    }
                }
                else if (d->inputIndex(k) >= 0)
                {
                    const bool isBool = d->inputs[d->inputIndex(k)].type == PortType::Bool;
                    const double value = isBool ? ((text == QLatin1String("true") || text == QLatin1String("1")) ? 1.0 : 0.0) : text.toDouble();
                    defaults[k] = QJsonArray{ value };
                }
            }
            o["params"] = params;
            o["defaults"] = defaults;
            nodes.append(o);
        }
        QJsonArray conns;
        for (const AnimBlendTreeLink& l : tree.links)
        {
            if (!inGraph.contains(l.fromNode) || !inGraph.contains(l.toNode)) continue;
            QJsonObject o;
            o["from"] = l.fromNode;
            o["fromPort"] = QString::fromStdString(l.fromPort);
            o["to"] = l.toNode;
            o["toPort"] = QString::fromStdString(l.toPort);
            conns.append(o);
        }
        QJsonObject root;
        root["format"] = domain().format();
        if (graphId == 0) root["model"] = QString::fromStdString(tree.modelPath);
        root["nodes"] = nodes;
        root["connections"] = conns;
        return root;
    }

} // namespace kme
