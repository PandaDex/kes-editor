#pragma once

#include "framework/graph/GraphModel.h"
#include "particle_asset.h"

namespace kpe
{
    // Particle dynamic-input graphs, built from the engine's node catalogue (ParticleGraphCatalog).
    class ParticleGraphDomain : public nodegraph::GraphDomain
    {
    public:
        static const ParticleGraphDomain& instance();

        const nodegraph::NodeCatalog& catalog() const override { return m_catalog; }
        QString format() const override { return QStringLiteral("kesParticleGraph"); }
        bool typesCompatible(nodegraph::PortType from, nodegraph::PortType to, QString* why) const override;
        QString inspectorHint() const override;

    private:
        ParticleGraphDomain();
        nodegraph::NodeCatalog m_catalog;
    };

    QJsonObject graphToJson(const ParticleGraphData& graph);
    // Keeps graph.id.
    void graphFromModel(const nodegraph::GraphModel& model, ParticleGraphData& graph);
    // A new graph with an Output node fed by a constant of the parameter's current value.
    ParticleGraphData makeStarterGraph(int id, int dim, const float value[4]);
}
