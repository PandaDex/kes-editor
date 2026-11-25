// Particle editor: templates simulate, graphs survive the editor's node graph round trip, undo restores snapshots.
#include "framework/graph/GraphModel.h"
#include "modules/particles/ParticleDocument.h"
#include "modules/particles/ParticleGraph.h"
#include "particle_graph.h"
#include "particle_sim.h"

#include <QCoreApplication>
#include <QTemporaryDir>
#include <cstdio>

static int g_failures = 0;
#define EXPECT(cond)                                                       \
    do                                                                     \
    {                                                                      \
        if (!(cond))                                                       \
        {                                                                  \
            std::printf("FAILED %s:%d: %s\n", __FILE__, __LINE__, #cond);  \
            ++g_failures;                                                  \
        }                                                                  \
    } while (0)

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);

    // Extra arguments: .kfx files that must load, compile and spawn (content checks).
    for (int i = 1; i < argc; ++i)
    {
        ParticleSystemData system;
        std::string error;
        const bool loaded = LoadParticleSystemFile(argv[i], system, &error);
        if (!loaded)
            std::printf("%s: %s\n", argv[i], error.c_str());
        EXPECT(loaded);
        ParticleSystemInstance instance;
        std::vector<std::string> errors;
        instance.Init(&system, 1, &errors);
        for (const std::string& e : errors)
            std::printf("%s: %s\n", argv[i], e.c_str());
        EXPECT(errors.empty());
        instance.SetTransform({}, {}, { 1, 1, 1 }, true);
        instance.Activate(true);
        for (int t = 0; t < 60; ++t)
            instance.Tick(1.0f / 30.0f);
        std::printf("%s: %zu emitter(s), %u particles after 2 s\n", argv[i], system.emitters.size(), instance.GetParticleCount());
    }

    // Every emitter template loads into the simulation without graph errors and spawns.
    for (const QString& name : kpe::emitterTemplateNames())
    {
        ParticleSystemData system;
        system.emitters.push_back(kpe::makeEmitterTemplate(name));
        ParticleSystemInstance instance;
        std::vector<std::string> errors;
        instance.Init(&system, 3, &errors);
        EXPECT(errors.empty());
        instance.SetTransform({}, {}, { 1, 1, 1 }, true);
        instance.Activate(true);
        for (int i = 0; i < 30; ++i)
        {
            instance.SetTransform({ float(i) * 0.1f, 0, 0 }, {}, { 1, 1, 1 });
            instance.Tick(1.0f / 30.0f);
        }
        if (instance.GetParticleCount() == 0)
            std::printf("template %s spawned nothing\n", qPrintable(name));
        EXPECT(instance.GetParticleCount() > 0);
    }

    // Graph -> editor model -> graph keeps nodes, params, defaults and links.
    {
        const float value[4] = { 0.5f, 0.25f, 1.0f, 1.0f };
        ParticleGraphData graph = kpe::makeStarterGraph(7, 4, value);
        graph.nodes.push_back({ 10, "Multiply", 0, 0, {}, { { 1, { 2.0f } } } });
        nodegraph::GraphModel model(&kpe::ParticleGraphDomain::instance());
        QStringList warnings;
        EXPECT(model.loadJson(kpe::graphToJson(graph), &warnings));
        EXPECT(warnings.isEmpty());
        ParticleGraphData back;
        back.id = 7;
        kpe::graphFromModel(model, back);
        EXPECT(back.nodes.size() == graph.nodes.size());
        EXPECT(back.links.size() == 1);
        ParticleSystemData system;
        ParticleEmitterData emitter;
        ParticleGraphProgram program;
        std::string error;
        EXPECT(program.Compile(back, emitter, system, &error));
        float out[4] = {};
        program.Run(ParticleEvalContext(), 0, 1, out);
        EXPECT(std::abs(out[1] - 0.25f) < 1e-4f);
        for (const ParticleGraphNode& n : back.nodes)
            if (n.type == "Multiply")
                EXPECT(n.FindInputDefault(1) && n.FindInputDefault(1)->at(0) == 2.0f);
    }

    // Document edits undo and redo; save/load round trips.
    {
        kpe::ParticleDocument doc;
        doc.reset(kpe::makeDefaultSystem());
        const size_t modules = doc.data().emitters[0].modules.size();
        doc.edit("Add", [](ParticleSystemData& d) { d.emitters[0].modules.push_back(MakeParticleModule("Drag", ParticleStage::ParticleUpdate)); }, true);
        doc.edit("Rate", [](ParticleSystemData& d) { d.warmup = 1.0f; }, false, "w");
        doc.edit("Rate", [](ParticleSystemData& d) { d.warmup = 2.0f; }, false, "w");
        EXPECT(doc.undoStack()->count() == 2);
        EXPECT(doc.data().warmup == 2.0f);
        doc.undoStack()->undo();
        EXPECT(doc.data().warmup == 0.0f);
        doc.undoStack()->undo();
        EXPECT(doc.data().emitters[0].modules.size() == modules);
        doc.undoStack()->redo();
        EXPECT(doc.data().emitters[0].modules.size() == modules + 1);

        QTemporaryDir dir;
        const QString path = dir.filePath("test.kfx");
        QString error;
        EXPECT(doc.saveAs(path, &error));
        EXPECT(!doc.isModified());
        kpe::ParticleDocument loaded;
        EXPECT(loaded.load(path, &error));
        EXPECT(WriteParticleSystemText(loaded.data()) == WriteParticleSystemText(doc.data()));
    }

    std::printf(g_failures ? "%d failure(s)\n" : "all passed\n", g_failures);
    return g_failures ? 1 : 0;
}
