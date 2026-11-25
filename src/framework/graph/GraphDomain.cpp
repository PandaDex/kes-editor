#include "framework/graph/GraphDomain.h"

#include <QtMath>

namespace nodegraph
{
    bool GraphDomain::typesCompatible(PortType, PortType, QString*) const
    {
        return true;
    }

    QColor GraphDomain::portColor(PortType t) const
    {
        switch (t)
        {
        case PortType::Float:   return QColor("#a0d468");
        case PortType::Float2:  return QColor("#48cfad");
        case PortType::Float3:  return QColor("#4fc1e9");
        case PortType::Float4:  return QColor("#5d9cec");
        case PortType::Int:     return QColor("#ac92ec");
        case PortType::Bool:    return QColor("#ed5565");
        case PortType::Generic: return QColor("#c8c8c8");
        case PortType::Pose:    return QColor("#f0963a");
        }
        return Qt::white;
    }

    QColor GraphDomain::categoryColor(const QString& c) const
    {
        static const QHash<QString, QColor> map = {
            { "Input", QColor("#3f7f5f") },
            { "Constant", QColor("#5f6f8f") },
            { "Property", QColor("#8f6f3f") },
            { "Math", QColor("#4a6fa5") },
            { "Vector", QColor("#6a5acd") },
            { "UV", QColor("#2f8f8f") },
            { "Texture", QColor("#a0522d") },
            { "Procedural", QColor("#8f4f8f") },
            { "Color", QColor("#b0603f") },
            { "Custom", QColor("#7f7f3f") },
            { "Output", QColor("#a03a3a") },
            { "Pose", QColor("#3f7f5f") },
            { "Blend", QColor("#4a6fa5") },
            { "State", QColor("#8f4f8f") },
            { "Parameter", QColor("#8f6f3f") },
            { "Logic", QColor("#5f6f8f") },
        };
        return map.value(c, QColor("#555566"));
    }

    QString GraphDomain::typeName(PortType t) const
    {
        switch (t)
        {
        case PortType::Float2: return QStringLiteral("float2");
        case PortType::Float3: return QStringLiteral("float3");
        case PortType::Float4: return QStringLiteral("float4");
        case PortType::Int:    return QStringLiteral("int");
        case PortType::Bool:   return QStringLiteral("bool");
        case PortType::Pose:   return QStringLiteral("pose");
        default:               return QStringLiteral("float");
        }
    }

    QHash<int, NodePortTypes> GraphDomain::resolveTypes(const GraphModel&) const
    {
        return {};
    }

    QString GraphDomain::inputSummary(const PortDef& port, const QVariantList& v, PortType resolved) const
    {
        const double first = v.isEmpty() ? 0.0 : v[0].toDouble();
        if (port.type == PortType::Bool) return first != 0.0 ? QStringLiteral("true") : QStringLiteral("false");
        if (port.type == PortType::Int) return QString::number(qRound(first));
        const PortType shown = resolved == PortType::Generic ? PortType::Float : resolved;
        if (port.type == PortType::Generic || dimension(port.type) <= 1 || dimension(shown) <= 1)
            return QString::number(first, 'g', 4);
        QStringList parts;
        for (int k = 0; k < dimension(shown); ++k)
            parts << QString::number(k < v.size() ? v[k].toDouble() : 0.0, 'g', 3);
        return "(" + parts.join(", ") + ")";
    }

    QString GraphDomain::paramSummary(const ParamDef& p, const QVariant& v) const
    {
        switch (p.kind)
        {
        case ParamKind::Float: return QString("%1: %2").arg(p.name).arg(v.toDouble(), 0, 'g', 5);
        case ParamKind::Int:   return QString("%1: %2").arg(p.name).arg(v.toInt());
        case ParamKind::Bool:  return QString("%1: %2").arg(p.name, v.toBool() ? "on" : "off");
        case ParamKind::Color: return QString("%1:").arg(p.name);
        case ParamKind::Clip:
        {
            const QString clip = v.toString().section('/', -1);
            return QString("%1: %2").arg(p.name, clip.isEmpty() ? QStringLiteral("(none)") : clip);
        }
        case ParamKind::Bone: return QString("%1: %2").arg(p.name, v.toString().isEmpty() ? QStringLiteral("(all)") : v.toString());
        default:              return QString("%1: %2").arg(p.name, v.toString());
        }
    }

    bool GraphDomain::showsDefault(const PortDef& port) const
    {
        return port.type != PortType::Pose;
    }

    QString GraphDomain::defaultNote(const PortDef&) const
    {
        return {};
    }
}
