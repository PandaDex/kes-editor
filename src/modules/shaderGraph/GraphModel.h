#pragma once

#include "MaterialSettings.h"
#include "NodeLibrary.h"
#include "framework/graph/GraphModel.h"

namespace sne
{

    using nodegraph::GraphConnection;
    using nodegraph::GraphNode;

    // Shader graph: the nodes plus the material values the graph starts from.
    class GraphModel : public nodegraph::GraphModel
    {
        Q_OBJECT
    public:
        explicit GraphModel(QObject* parent = nullptr);

        const MaterialSettings& material() const { return m_material; }
        void setMaterial(const MaterialSettings& material);

    signals:
        void materialChanged();

    protected:
        void writeDocument(QJsonObject& root) const override;
        void readDocument(const QJsonObject& root) override;
        void clearDocument() override;

    private:
        MaterialSettings m_material;
    };

} // namespace sne
