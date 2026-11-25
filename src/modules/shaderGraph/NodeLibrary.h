#pragma once

#include "framework/graph/GraphDomain.h"

#include <QHash>
#include <QStringList>
#include <QVariant>
#include <QVector>

namespace sne
{

    using nodegraph::dimension;
    using nodegraph::floatTypeOfDim;
    using nodegraph::isFloatFamily;
    using nodegraph::NodeDef;
    using nodegraph::NodeRole;
    using nodegraph::OutputDef;
    using nodegraph::ParamDef;
    using nodegraph::ParamKind;
    using nodegraph::PortDef;
    using nodegraph::PortType;

    inline QString slangTypeName(PortType t)
    {
        switch (t)
        {
        case PortType::Float2: return QStringLiteral("float2");
        case PortType::Float3: return QStringLiteral("float3");
        case PortType::Float4: return QStringLiteral("float4");
        case PortType::Int:    return QStringLiteral("int");
        case PortType::Bool:   return QStringLiteral("bool");
        default:               return QStringLiteral("float");
        }
    }

    struct HelperDef
    {
        QString key;
        QStringList deps;
        QString code;
    };

    // The shader graph's nodes and the Slang helper functions they call.
    class NodeLibrary
    {
    public:
        static const NodeLibrary& instance();

        const nodegraph::NodeCatalog& catalog() const { return m_catalog; }
        const NodeDef* find(const QString& type) const { return m_catalog.find(type); }
        const QVector<NodeDef>& all() const { return m_catalog.all(); }
        QStringList categories() const { return m_catalog.categories(); }
        const QVector<HelperDef>& helpers() const { return m_helpers; }
        const HelperDef* helper(const QString& key) const;

    private:
        NodeLibrary();
        nodegraph::NodeCatalog m_catalog;
        QVector<HelperDef> m_helpers;
    };

    class ShaderGraphDomain : public nodegraph::GraphDomain
    {
    public:
        static const ShaderGraphDomain& instance();

        const nodegraph::NodeCatalog& catalog() const override { return NodeLibrary::instance().catalog(); }
        QString format() const override { return QStringLiteral("kesGraph"); }
        QHash<int, nodegraph::NodePortTypes> resolveTypes(const nodegraph::GraphModel& graph) const override;
        QString inputSummary(const PortDef& port, const QVariantList& value, PortType resolved) const override;
        QString defaultNote(const PortDef& port) const override;
    };

} // namespace sne
