#include "GraphModel.h"
#include "SlangCodeGen.h"

namespace sne
{

    GraphModel::GraphModel(QObject* parent)
        : nodegraph::GraphModel(&ShaderGraphDomain::instance(), parent)
    {
        connect(this, &nodegraph::GraphModel::documentChanged, this, &GraphModel::materialChanged);
    }

    void GraphModel::setMaterial(const MaterialSettings& material)
    {
        m_material = material;
        emit materialChanged();
        emit changed();
    }

    void GraphModel::writeDocument(QJsonObject& root) const
    {
        root["material"] = m_material.toJson();
    }

    void GraphModel::readDocument(const QJsonObject& root)
    {
        m_material = MaterialSettings::fromJson(root.value("material").toObject());
    }

    void GraphModel::clearDocument()
    {
        m_material = {};
    }

    const ShaderGraphDomain& ShaderGraphDomain::instance()
    {
        static const ShaderGraphDomain domain;
        return domain;
    }

    QHash<int, nodegraph::NodePortTypes> ShaderGraphDomain::resolveTypes(const nodegraph::GraphModel& graph) const
    {
        const TypeInfo ti = inferTypes(graph);
        QHash<int, nodegraph::NodePortTypes> out;
        for (int id : graph.nodeIds())
        {
            const NodeDef* d = graph.def(id);
            if (!d) continue;
            nodegraph::NodePortTypes types;
            for (int i = 0; i < d->inputs.size(); ++i)
            {
                PortType t = d->inputs[i].type == PortType::Generic ? ti.genericType(id) : d->inputs[i].type;
                if (d->role == NodeRole::CustomExpression)
                {
                    const GraphConnection* c = graph.inputConnection(id, i);
                    t = c ? ti.outputType(c->fromNode, c->fromPort) : PortType::Float;
                }
                types.inputs << t;
            }
            for (int o = 0; o < d->outputs.size(); ++o)
                types.outputs << ti.outputType(id, o);
            out.insert(id, types);
        }
        return out;
    }

    QString ShaderGraphDomain::inputSummary(const PortDef& port, const QVariantList& value, PortType resolved) const
    {
        if (!port.defaultExpr.isEmpty())
            return port.defaultExpr.startsWith(QLatin1String("surface.")) ? QStringLiteral("material") : QStringLiteral("mesh");
        return nodegraph::GraphDomain::inputSummary(port, value, resolved);
    }

    QString ShaderGraphDomain::defaultNote(const PortDef& port) const
    {
        if (port.defaultExpr.isEmpty()) return {};
        return port.defaultExpr.startsWith(QLatin1String("surface.")) ? QStringLiteral("uses the Material panel when unconnected")
                                                                      : QStringLiteral("uses mesh data when unconnected");
    }

} // namespace sne
