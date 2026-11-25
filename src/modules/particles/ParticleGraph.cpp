#include "ParticleGraph.h"

#include "particle_graph.h"

#include <QJsonArray>

namespace kpe
{
    using nodegraph::NodeDef;
    using nodegraph::NodeRole;
    using nodegraph::ParamDef;
    using nodegraph::ParamKind;
    using nodegraph::PortType;

    namespace
    {
        PortType portType(int dim)
        {
            switch (dim)
            {
            case 0: return PortType::Generic;
            case 2: return PortType::Float2;
            case 3: return PortType::Float3;
            case 4: return PortType::Float4;
            default: return PortType::Float;
            }
        }

        QVariantList defaultList(const ParticleGraphPortDef& p)
        {
            const int n = p.dim <= 1 ? 1 : p.dim;
            QVariantList out;
            for (int i = 0; i < n; ++i)
                out << double(p.def[i]);
            return out;
        }

        QVariant paramValue(const ParticleGraphParamDef& p, const QString& text)
        {
            switch (p.kind)
            {
            case ParticleParamKind::Value:
                if (p.dim == 4)
                {
                    QVariantList c;
                    const QStringList parts = text.split(' ', Qt::SkipEmptyParts);
                    for (int i = 0; i < 4; ++i)
                        c << (i < parts.size() ? parts[i].toDouble() : 1.0);
                    return c;
                }
                return text.toDouble();
            case ParticleParamKind::Int: return text.toInt();
            case ParticleParamKind::Bool: return text == QLatin1String("true") || text == QLatin1String("1");
            default: return text;
            }
        }

        QString paramText(const QVariant& v)
        {
            if (v.typeId() == QMetaType::Bool)
                return v.toBool() ? QStringLiteral("true") : QStringLiteral("false");
            if (v.typeId() == QMetaType::Double || v.typeId() == QMetaType::Float)
                return QString::number(v.toDouble(), 'g', 9);
            if (v.typeId() == QMetaType::QVariantList)
            {
                QStringList parts;
                for (const QVariant& c : v.toList())
                    parts << QString::number(c.toDouble(), 'g', 9);
                return parts.join(' ');
            }
            return v.toString();
        }
    }

    const ParticleGraphDomain& ParticleGraphDomain::instance()
    {
        static const ParticleGraphDomain domain;
        return domain;
    }

    ParticleGraphDomain::ParticleGraphDomain()
    {
        m_catalog.setCategoryOrder({ "Inputs", "Constants", "Math", "Vector", "Random", "Curves", "Output" });
        for (const ParticleGraphNodeDef& info : ParticleGraphCatalog())
        {
            NodeDef& d = m_catalog.add(QString::fromStdString(info.type), QString::fromStdString(info.title),
                QString::fromStdString(info.category), QString::fromStdString(info.description));
            d.role = info.isOutput ? NodeRole::Output : NodeRole::Normal;
            for (const ParticleGraphPortDef& p : info.inputs)
                d.in(QString::fromStdString(p.name), portType(p.dim), defaultList(p));
            for (const ParticleGraphPortDef& p : info.outputs)
                d.out(QString::fromStdString(p.name), portType(p.dim));
            for (const ParticleGraphParamDef& p : info.params)
            {
                ParamDef pd;
                pd.name = QString::fromStdString(p.name);
                switch (p.kind)
                {
                case ParticleParamKind::Value: pd.kind = p.dim == 4 ? ParamKind::Color : ParamKind::Float; break;
                case ParticleParamKind::Int: pd.kind = ParamKind::Int; break;
                case ParticleParamKind::Bool: pd.kind = ParamKind::Bool; break;
                case ParticleParamKind::Enum: pd.kind = ParamKind::Enum; break;
                default: pd.kind = pd.name == QLatin1String("Name") ? ParamKind::Identifier : ParamKind::Code; break;
                }
                pd.defaultValue = paramValue(p, QString::fromStdString(p.defaultText));
                pd.options = QString::fromStdString(p.options).split('|', Qt::SkipEmptyParts);
                pd.step = 0.05;
                d.params.append(pd);
            }
        }
    }

    bool ParticleGraphDomain::typesCompatible(PortType from, PortType to, QString* why) const
    {
        const auto numeric = [](PortType t) { return nodegraph::isFloatFamily(t) || t == PortType::Generic; };
        if (numeric(from) && numeric(to))
            return true;
        if (why)
            *why = QStringLiteral("Only numbers and vectors connect");
        return false;
    }

    QString ParticleGraphDomain::inspectorHint() const
    {
        return QStringLiteral("Evaluated for every particle. Scalars widen to vectors; wider values are cut to what the parameter needs.");
    }

    QJsonObject graphToJson(const ParticleGraphData& graph)
    {
        const ParticleGraphDomain& domain = ParticleGraphDomain::instance();
        QJsonArray nodes;
        for (const ParticleGraphNode& n : graph.nodes)
        {
            const QString type = QString::fromStdString(n.type);
            const NodeDef* d = domain.catalog().find(type);
            const ParticleGraphNodeDef* info = FindParticleGraphNodeDef(n.type);
            if (!d || !info)
                continue;
            QJsonObject o;
            o["id"] = n.id;
            o["type"] = type;
            o["x"] = n.x;
            o["y"] = n.y;
            QJsonObject params;
            for (const auto& [key, text] : n.params)
            {
                for (const ParticleGraphParamDef& p : info->params)
                {
                    if (p.name == key)
                        params[QString::fromStdString(key)] = QJsonValue::fromVariant(paramValue(p, QString::fromStdString(text)));
                }
            }
            QJsonObject defaults;
            for (const auto& [port, values] : n.inputDefaults)
            {
                if (port < 0 || port >= d->inputs.size())
                    continue;
                QJsonArray a;
                for (float v : values)
                    a.append(double(v));
                defaults[d->inputs[port].name] = a;
            }
            o["params"] = params;
            o["defaults"] = defaults;
            nodes.append(o);
        }
        QJsonArray connections;
        for (const ParticleGraphLink& l : graph.links)
        {
            const ParticleGraphNode* from = nullptr;
            const ParticleGraphNode* to = nullptr;
            for (const ParticleGraphNode& n : graph.nodes)
            {
                if (n.id == l.fromNode) from = &n;
                if (n.id == l.toNode) to = &n;
            }
            const NodeDef* df = from ? domain.catalog().find(QString::fromStdString(from->type)) : nullptr;
            const NodeDef* dt = to ? domain.catalog().find(QString::fromStdString(to->type)) : nullptr;
            if (!df || !dt || l.fromPort >= df->outputs.size() || l.toPort >= dt->inputs.size())
                continue;
            QJsonObject o;
            o["from"] = l.fromNode;
            o["fromPort"] = df->outputs[l.fromPort].name;
            o["to"] = l.toNode;
            o["toPort"] = dt->inputs[l.toPort].name;
            connections.append(o);
        }
        QJsonObject root;
        root["format"] = domain.format();
        root["nodes"] = nodes;
        root["connections"] = connections;
        return root;
    }

    void graphFromModel(const nodegraph::GraphModel& model, ParticleGraphData& graph)
    {
        graph.nodes.clear();
        graph.links.clear();
        for (int id : model.nodeIds())
        {
            const nodegraph::GraphNode& n = *model.node(id);
            const NodeDef* d = model.def(id);
            if (!d)
                continue;
            ParticleGraphNode out;
            out.id = id;
            out.type = n.type.toStdString();
            out.x = float(n.pos.x());
            out.y = float(n.pos.y());
            for (const ParamDef& p : d->params)
                out.params.emplace_back(p.name.toStdString(), paramText(model.param(id, p.name)).toStdString());
            for (int i = 0; i < d->inputs.size(); ++i)
            {
                if (model.inputConnection(id, i))
                    continue;
                const QVariantList v = model.inputDefault(id, i);
                if (v.isEmpty())
                    continue;
                std::vector<float> values;
                for (const QVariant& c : v)
                    values.push_back(float(c.toDouble()));
                out.inputDefaults.emplace_back(i, std::move(values));
            }
            graph.nodes.push_back(std::move(out));
        }
        for (const nodegraph::GraphConnection& c : model.connections())
            graph.links.push_back({ c.fromNode, c.fromPort, c.toNode, c.toPort });
    }

    ParticleGraphData makeStarterGraph(int id, int dim, const float value[4])
    {
        ParticleGraphData g;
        g.id = id;
        ParticleGraphNode output;
        output.id = 1;
        output.type = "Output";
        output.x = 260.0f;
        g.nodes.push_back(output);

        ParticleGraphNode attributes;
        attributes.id = 2;
        attributes.type = "ParticleAttributes";
        attributes.x = -260.0f;
        attributes.y = -80.0f;
        g.nodes.push_back(attributes);

        ParticleGraphNode constant;
        constant.id = 3;
        if (dim == 4)
        {
            constant.type = "Color";
            constant.params.emplace_back("Color", std::to_string(value[0]) + " " + std::to_string(value[1]) + " " + std::to_string(value[2]) + " " +
                std::to_string(value[3]));
        }
        else if (dim >= 2)
        {
            constant.type = "Vector";
            constant.params = { { "X", std::to_string(value[0]) }, { "Y", std::to_string(value[1]) }, { "Z", std::to_string(value[2]) } };
        }
        else
        {
            constant.type = "Float";
            constant.params.emplace_back("Value", std::to_string(value[0]));
        }
        constant.x = 0.0f;
        constant.y = 60.0f;
        g.nodes.push_back(constant);
        g.links.push_back({ 3, 0, 1, 0 });
        return g;
    }
}
