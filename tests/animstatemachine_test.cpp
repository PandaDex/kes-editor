// State machines in the animation graph editor: document, undo, file round trip, and the tab's
// navigation/inspector wiring (widgets are built but never shown).
#include "core/EditorContext.h"
#include "framework/graph/GraphEditor.h"
#include "framework/graph/NodeGraphScene.h"
#include "modules/modelEditor/AnimGraphDocument.h"
#include "modules/modelEditor/GraphModel.h"
#include "modules/modelEditor/ModelEditorModules.h"
#include "modules/modelEditor/StateMachineEditor.h"

#include <QAction>
#include <QApplication>
#include <QDoubleSpinBox>
#include <QTemporaryDir>
#include <QToolButton>
#include <QUndoStack>
#include <cstdio>
#include <cstdlib>

namespace
{

    int g_failures = 0;

#define EXPECT(cond)                                              \
    do                                                            \
    {                                                             \
        if (!(cond))                                              \
        {                                                         \
            std::printf("FAILED line %d: %s\n", __LINE__, #cond); \
            ++g_failures;                                         \
        }                                                         \
    } while (0)

    QAction* findAction(QObject* root, const QString& text)
    {
        // undo actions carry the command name after the label
        for (QAction* a : root->findChildren<QAction*>())
            if (a->text() == text || a->text().startsWith(text + ' ')) return a;
        std::printf("no action %s\n", qPrintable(text));
        std::exit(1);
    }

    nodegraph::NodeGraphScene* sceneFor(QObject* root, const nodegraph::GraphModel* model)
    {
        for (auto* s : root->findChildren<nodegraph::NodeGraphScene*>())
            if (s->model() == model) return s;
        return nullptr;
    }

} // namespace

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    QTemporaryDir tmp;

    // ---- document
    kme::AnimGraphDocument doc;
    doc.newDocument();
    kme::GraphModel* root = doc.root();
    EXPECT(root->nodeCount() == 1);
    const QJsonObject empty = doc.snapshot();

    int sm = -1;
    root->edit("Add machine", [&]
        {
        sm = root->addNode("StateMachine", QPointF(0, 0));
        root->addConnection(sm, 0, root->outputNodeId(), 0); });
    kme::StateMachineModel* machine = doc.machine(root, sm);
    EXPECT(machine != nullptr);
    if (!machine) return 1;
    EXPECT(machine->states().size() == 1);
    const int idle = machine->states().first().id;
    EXPECT(machine->entryState() == idle);
    EXPECT(machine->states().first().graph->nodeCount() == 1);

    int walk = 0;
    doc.edit("Build machine", [&]
        {
        walk = machine->addState(QPointF(300, 0), "Walk");
        machine->setStateName(walk, "idle"); // clashes with Idle, so it gets a number
        machine->setStateName(walk, "Walk");
        const int toWalk = machine->addTransition(idle, walk);
        kme::AnimTransition t = *machine->transition(toWalk);
        t.duration = 0.4;
        t.conditions = { { "Speed", AnimConditionOp::Greater, 0.5 } };
        machine->setTransition(t);
        const int back = machine->addTransition(walk, idle);
        kme::AnimTransition b = *machine->transition(back);
        b.conditions = { { "Stop", AnimConditionOp::Triggered, 0.0 } };
        b.useExitTime = true;
        b.exitTime = 0.8;
        machine->setTransition(b);
        machine->addTransition(kme::StateMachineModel::AnyState, idle);
        kme::GraphModel* walkGraph = const_cast<kme::GraphModel*>(machine->state(walk)->graph);
        const int clip = walkGraph->addNode("Clip", QPointF(0, 0));
        walkGraph->setParam(clip, "Clip", "anims/walk.kanim");
        walkGraph->addConnection(clip, 0, walkGraph->outputNodeId(), 0); });
    EXPECT(machine->stateName(walk) == "Walk");
    EXPECT(machine->transitions().size() == 3);
    EXPECT(doc.parameterNames() == QStringList({ "Speed", "Stop" }));

    kme::AnimGraphIds ids;
    const AnimBlendTreeData tree = doc.toBlendTree(&ids);
    EXPECT(tree.states.size() == 2);
    EXPECT(tree.transitions.size() == 3);
    EXPECT(ids.states.value(machine).size() == 2);
    AnimBlendTreeInstance instance;
    std::string err;
    EXPECT(instance.Build(tree, &err));
    if (!err.empty()) std::printf("build: %s\n", err.c_str());

    // undo everything, then redo: the snapshot must come back the same
    const QJsonObject full = doc.snapshot();
    while (doc.undoStack()->canUndo())
        doc.undoStack()->undo();
    EXPECT(doc.snapshot() == empty);
    EXPECT(doc.machine(root, sm) == nullptr);
    while (doc.undoStack()->canRedo())
        doc.undoStack()->redo();
    EXPECT(doc.snapshot() == full);
    machine = doc.machine(root, sm);
    EXPECT(machine && machine->states().size() == 2);

    // deleting the node removes its machine; undo restores it with its state graphs
    root->edit("Delete", [&]
        { root->removeNode(sm); });
    EXPECT(doc.machine(root, sm) == nullptr);
    doc.undoStack()->undo();
    EXPECT(doc.snapshot() == full);
    machine = doc.machine(root, sm);
    EXPECT(machine != nullptr);

    // file round trip
    const QString path = tmp.filePath("machine.ag");
    EXPECT(SaveAnimBlendTreeFile(path.toUtf8().constData(), doc.toBlendTree(), &err));
    AnimBlendTreeData loaded;
    EXPECT(LoadAnimBlendTreeFile(path.toUtf8().constData(), loaded, &err));
    kme::AnimGraphDocument doc2;
    doc2.loadBlendTree(loaded);
    QList<kme::StateMachineModel*> machines = doc2.machinesIn(doc2.root());
    EXPECT(machines.size() == 1);
    if (machines.size() == 1)
    {
        kme::StateMachineModel* m = machines.first();
        EXPECT(m->states().size() == 2);
        EXPECT(m->stateName(m->entryState()) == "Idle");
        EXPECT(m->transitions().size() == 3);
        const kme::AnimTransition& back = m->transitions()[1];
        EXPECT(back.useExitTime && std::abs(back.exitTime - 0.8) < 1e-5);
        EXPECT(back.conditions.size() == 1 && back.conditions[0].op == AnimConditionOp::Triggered);
        EXPECT(m->transitions()[2].from == kme::StateMachineModel::AnyState);
        EXPECT(m->states()[1].graph->nodeCount() == 2);
        EXPECT(m->states()[1].graph->connections().size() == 1);
    }
    EXPECT(doc2.toBlendTree().nodes.size() == doc.toBlendTree().nodes.size());
    EXPECT(!doc2.undoStack()->canUndo());

    // ---- tab
    EditorContext ctx;
    {
        kme::AnimGraphTab tab(&ctx);
        QString error;
        EXPECT(tab.openFile(path, &error));
        app.processEvents();
        auto* graphDoc = tab.findChild<kme::AnimGraphDocument*>();
        EXPECT(graphDoc != nullptr);
        if (!graphDoc) return 1;
        kme::GraphModel* tabRoot = graphDoc->root();
        kme::StateMachineModel* m = graphDoc->machinesIn(tabRoot).value(0);
        EXPECT(m != nullptr);
        if (!m) return 1;

        emit sceneFor(&tab, tabRoot)->nodeDoubleClicked(m->nodeId());
        app.processEvents();
        auto* smEditor = tab.findChild<kme::StateMachineEditor*>();
        EXPECT(smEditor != nullptr);
        if (!smEditor) return 1;
        EXPECT(smEditor->scene()->stateItem(kme::StateMachineModel::AnyState) != nullptr);

        const int walkId = m->states()[1].id;
        kme::GraphModel* walkGraph = m->states()[1].graph;
        emit smEditor->stateOpened(walkId);
        app.processEvents();
        EXPECT(sceneFor(&tab, walkGraph) != nullptr);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        int crumbs = 0;
        for (QToolButton* b : tab.findChildren<QToolButton*>())
            if (b->text() == "Walk" || b->text() == "Locomotion") ++crumbs;
        EXPECT(crumbs == 2);

        // delete the open state from the machine view, then undo it
        smEditor->scene()->selectState(walkId);
        smEditor->scene()->deleteSelection();
        app.processEvents();
        EXPECT(m->states().size() == 1);
        EXPECT(sceneFor(&tab, walkGraph) == nullptr);
        EXPECT(tab.isModified());
        findAction(&tab, "&Undo")->trigger();
        app.processEvents();
        EXPECT(m->states().size() == 2);
        EXPECT(m->transitions().size() == 3);

        // inspector edits a transition
        const int firstTransition = m->transitions()[0].id;
        smEditor->scene()->clearSelection();
        for (QGraphicsItem* item : smEditor->scene()->items())
            if (item->type() == kme::TransitionItem::Type && static_cast<kme::TransitionItem*>(item)->transitionId() == firstTransition)
                item->setSelected(true);
        app.processEvents();
        QDoubleSpinBox* blend = nullptr;
        for (QDoubleSpinBox* sb : smEditor->inspector()->findChildren<QDoubleSpinBox*>())
            if (sb->suffix() == " s") blend = sb;
        EXPECT(blend != nullptr);
        if (blend) blend->setValue(1.25);
        EXPECT(std::abs(m->transition(firstTransition)->duration - 1.25) < 1e-6);

        // deleting the machine node while inside it drops back to the top graph
        emit smEditor->stateOpened(m->states()[0].id);
        app.processEvents();
        const int machineNode = m->nodeId();
        tabRoot->edit("Delete", [&]
            { tabRoot->removeNode(machineNode); });
        app.processEvents();
        EXPECT(tab.findChildren<kme::StateMachineEditor*>().isEmpty());
        findAction(&tab, "&Undo")->trigger();
        app.processEvents();
        EXPECT(graphDoc->machinesIn(tabRoot).size() == 1);
        findAction(&tab, "&Up One Level")->trigger();
        app.processEvents();
    }

    std::printf(g_failures ? "%d check(s) failed\n" : "all checks passed\n", g_failures);
    return g_failures ? 1 : 0;
}
