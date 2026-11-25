#pragma once

#include "framework/graph/GraphTypes.h"

#include <QColor>
#include <QHash>

namespace nodegraph
{
    class GraphModel;

    struct NodePortTypes
    {
        QVector<PortType> inputs;
        QVector<PortType> outputs;
    };

    // What makes one kind of graph different from another. The defaults suit simple value graphs.
    class GraphDomain
    {
    public:
        virtual ~GraphDomain() = default;

        virtual const NodeCatalog& catalog() const = 0;
        // "format" field of the JSON (files and clipboard)
        virtual QString format() const = 0;

        virtual bool typesCompatible(PortType from, PortType to, QString* why) const;

        virtual QColor portColor(PortType t) const;
        virtual QColor categoryColor(const QString& category) const;
        virtual QString typeName(PortType t) const;

        // Port types after resolving Generic ports; nodes left out use their declared types.
        virtual QHash<int, NodePortTypes> resolveTypes(const GraphModel& graph) const;

        // Short text drawn next to an unconnected input.
        virtual QString inputSummary(const PortDef& port, const QVariantList& value, PortType resolved) const;
        virtual QString paramSummary(const ParamDef& param, const QVariant& value) const;

        // Inspector: false hides the input from "Unconnected inputs"; a note replaces the editor.
        virtual bool showsDefault(const PortDef& port) const;
        virtual QString defaultNote(const PortDef& port) const;
        virtual QString inspectorHint() const { return {}; }
    };
}
