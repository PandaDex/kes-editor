#pragma once

#include "framework/graph/GraphModel.h"

#include <QHash>
#include <cstdint>

struct AnimBlendTreeData;

namespace kme
{

    using nodegraph::GraphConnection;
    using nodegraph::GraphNode;
    using nodegraph::NodeDef;
    using nodegraph::NodeRole;
    using nodegraph::ParamDef;
    using nodegraph::ParamKind;
    using nodegraph::PortDef;
    using nodegraph::PortType;

    // Animation blend tree nodes, built from the engine's node catalogue (GetAnimNodeCatalog).
    class AnimGraphDomain : public nodegraph::GraphDomain
    {
    public:
        static const AnimGraphDomain& instance();

        const nodegraph::NodeCatalog& catalog() const override { return m_catalog; }
        QString format() const override { return QStringLiteral("kesAnimGraph"); }
        bool typesCompatible(PortType from, PortType to, QString* why) const override;
        QString inputSummary(const PortDef& port, const QVariantList& value, PortType resolved) const override;
        QString inspectorHint() const override;

    private:
        AnimGraphDomain();
        nodegraph::NodeCatalog m_catalog;
    };

    // An animation graph (.ag) and the model it previews on.
    class GraphModel : public nodegraph::GraphModel
    {
        Q_OBJECT
    public:
        explicit GraphModel(QObject* parent = nullptr);

        const QString& modelPath() const { return m_modelPath; }
        void setModelPath(const QString& path);

        // This graph alone; AnimGraphDocument handles state machines.
        AnimBlendTreeData toBlendTree() const;
        void loadBlendTree(const AnimBlendTreeData& tree, QStringList* warnings = nullptr);

        // Appends this graph's nodes and links as graph `graphId` of the file, numbering nodes from
        // nextNodeId. localToFile receives the id each node got.
        void appendToBlendTree(AnimBlendTreeData& tree, int32_t graphId, int32_t& nextNodeId, QHash<int, int>* localToFile) const;
        // The JSON loadJson takes for the nodes of graph `graphId` of a file.
        QJsonObject graphJson(const AnimBlendTreeData& tree, int32_t graphId) const;

    signals:
        void modelPathChanged();

    protected:
        void writeDocument(QJsonObject& root) const override;
        void readDocument(const QJsonObject& root) override;
        void clearDocument() override;

    private:
        QString m_modelPath;
    };

} // namespace kme
