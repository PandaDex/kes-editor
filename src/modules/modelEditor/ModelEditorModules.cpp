#include "ModelEditorModules.h"

#include "AnimGraphDocument.h"
#include "AnimationPanel.h"
#include "StateMachineEditor.h"
#include "AttachmentPanel.h"
#include "GraphModel.h"
#include "GraphParamsPanel.h"
#include "KesProject.h"
#include "ModelDocument.h"
#include "ModelViewport.h"
#include "PreviewState.h"
#include "SkeletonPanel.h"
#include "anim_blend_tree.h"
#include "core/EditorContext.h"
#include "framework/graph/GraphEditor.h"
#include "framework/graph/InspectorWidget.h"
#include "framework/graph/NodeGraphScene.h"
#include "framework/graph/NodeGraphView.h"
#include "ui/KesStyle.h"

#include <QAction>
#include <QActionGroup>
#include <QComboBox>
#include <QDir>
#include <QDirIterator>
#include <QDockWidget>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QScrollArea>
#include <QSettings>
#include <QSplitter>
#include <QStackedWidget>
#include <QVBoxLayout>
#include <QStatusBar>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QUndoStack>
#include <algorithm>

namespace kme
{

    namespace
    {

        QScrollArea* scrolled(QWidget* w)
        {
            auto* area = new QScrollArea;
            area->setWidget(w);
            area->setWidgetResizable(true);
            area->setFrameShape(QFrame::NoFrame);
            return area;
        }

        QDockWidget* addDock(QMainWindow* window, const QString& title, const QString& name, QWidget* w, Qt::DockWidgetArea area)
        {
            auto* d = new QDockWidget(title, window);
            d->setObjectName(name);
            d->setWidget(w);
            window->addDockWidget(area, d);
            return d;
        }

        QAction* makeAction(QObject* parent, const QString& text, const QKeySequence& ks, std::function<void()> slot, const QString& icon = {})
        {
            auto* a = new QAction(text, parent);
            if (!ks.isEmpty()) a->setShortcut(ks);
            if (!icon.isEmpty()) a->setIcon(kes::style::icon(icon));
            if (slot) QObject::connect(a, &QAction::triggered, parent, std::move(slot));
            return a;
        }

        QString lastDir(const PreviewState* preview)
        {
            const QString dir = QSettings().value("modelEditor/lastDir").toString();
            return dir.isEmpty() ? preview->contentRoot() : dir;
        }

        void rememberDir(const QString& file)
        {
            QSettings().setValue("modelEditor/lastDir", QFileInfo(file).absolutePath());
        }

        QString toContentPath(const PreviewState* preview, const QString& absolutePath)
        {
            const QString rel = contentRelativePath(preview->contentRoot(), absolutePath);
            return rel.isEmpty() ? QDir::fromNativeSeparators(absolutePath) : rel;
        }

        // The project's content folder when the file is inside it, else the nearest one above the file.
        QString contentRootFor(const EditorContext* ctx, const QString& file)
        {
            const QString project = ctx->projectDir();
            if (file.isEmpty() || !contentRelativePath(project, file).isEmpty()) return project;
            const QString found = findContentRoot(file);
            return found.isEmpty() ? project : found;
        }

        // <model>_<clip>.kanim first
        QStringList scanClips(const PreviewState* preview, const QString& modelPath)
        {
            QStringList clips;
            const QString root = preview->contentRoot();
            const QString searchDir = !root.isEmpty() ? root : (modelPath.isEmpty() ? QString() : QFileInfo(modelPath).absolutePath());
            if (!searchDir.isEmpty())
            {
                QDirIterator it(searchDir, { "*.kanim" }, QDir::Files, QDirIterator::Subdirectories);
                while (it.hasNext())
                    clips << toContentPath(preview, it.next());
            }
            const QString stem = modelPath.isEmpty() ? QString() : QFileInfo(modelPath).completeBaseName() + "_";
            std::stable_sort(clips.begin(), clips.end(), [&](const QString& a, const QString& b)
                {
        const bool am = !stem.isEmpty() && QFileInfo(a).fileName().startsWith(stem, Qt::CaseInsensitive);
        const bool bm = !stem.isEmpty() && QFileInfo(b).fileName().startsWith(stem, Qt::CaseInsensitive);
        if (am != bm) return am;
        return a.compare(b, Qt::CaseInsensitive) < 0; });
            return clips;
        }

        QComboBox* pathCombo(const QStringList& choices, const QString& current, const QString& emptyLabel)
        {
            auto* combo = new QComboBox;
            combo->addItem(emptyLabel, QString());
            for (const QString& c : choices)
                combo->addItem(c, c);
            if (!current.isEmpty() && combo->findData(current) < 0) combo->addItem(current + "  (missing)", current);
            combo->setCurrentIndex(std::max(0, combo->findData(current)));
            combo->setMinimumContentsLength(14);
            combo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
            return combo;
        }

        void addOverlayActions(QMenu* menu, QObject* owner, ModelViewport* view)
        {
            const auto overlay = [&](const QString& text, bool ModelViewport::Overlays::* member)
            {
                auto* a = menu->addAction(text);
                a->setCheckable(true);
                a->setChecked(view->overlays().*member);
                QObject::connect(a, &QAction::toggled, owner, [view, member](bool on)
                    {
            ModelViewport::Overlays o = view->overlays();
            o.*member = on;
            view->setOverlays(o); });
            };
            overlay("Show &Mesh", &ModelViewport::Overlays::mesh);
            overlay("Show S&keleton", &ModelViewport::Overlays::skeleton);
            overlay("Show &Sockets", &ModelViewport::Overlays::sockets);
            overlay("Show &Colliders", &ModelViewport::Overlays::colliders);
            overlay("Show C&hild Models", &ModelViewport::Overlays::children);
            overlay("Show &Grid", &ModelViewport::Overlays::grid);
        }

    } // namespace

    // ---------------------------------------------------------------------------------------------
    // Model tab

    ModelEditorTab::ModelEditorTab(EditorContext* ctx, QWidget* parent)
        : EditorTab(ctx, QStringLiteral("model"), parent)
    {
        m_doc = new ModelDocument(this);
        m_preview = new PreviewState(m_doc, nullptr, this);
        m_view = new ModelViewport(m_doc, m_preview);
        setCentralWidget(m_view);

        m_skeleton = new SkeletonPanel(m_doc, m_preview);
        addDock(this, "Skeleton", "SkeletonDock", m_skeleton, Qt::LeftDockWidgetArea);
        m_attachments = new AttachmentPanel(m_doc, m_preview);
        QDockWidget* props = addDock(this, "Properties", "PropertiesDock", scrolled(m_attachments), Qt::RightDockWidgetArea);
        m_animation = new AnimationPanel(m_preview, false);
        addDock(this, "Animation", "AnimationDock", m_animation, Qt::BottomDockWidgetArea);

        m_statusContent = new QLabel;
        statusBar()->addPermanentWidget(m_statusContent);

        connect(m_doc, &ModelDocument::modifiedChanged, this, &EditorTab::titleChanged);
        connect(m_doc, &ModelDocument::selectionChanged, this, [this, props]
            {
        if (m_doc->selection().kind != Selection::None) props->raise(); });
        connect(m_animation, &AnimationPanel::browseClipRequested, this, [this]
            {
        const QString path = QFileDialog::getOpenFileName(this, "Open Clip", lastDir(m_preview), "Animation clips (*.kanim)");
        if (path.isEmpty()) return;
        rememberDir(path);
        m_animation->selectClip(toContentPath(m_preview, path)); });

        buildActions();
        updateContentRoot();
        restoreLayout();
    }

    void ModelEditorTab::buildActions()
    {
        QAction* undo = m_doc->undoStack()->createUndoAction(this, "&Undo");
        undo->setShortcut(QKeySequence::Undo);
        undo->setIcon(kes::style::icon("undo"));
        QAction* redo = m_doc->undoStack()->createRedoAction(this, "&Redo");
        redo->setShortcuts({ QKeySequence::Redo, QKeySequence("Ctrl+Y") });
        redo->setIcon(kes::style::icon("redo"));

        QMenu* edit = addTabMenu("&Edit");
        edit->addActions({ undo, redo });
        edit->addSeparator();
        edit->addAction(makeAction(this, "&Delete", QKeySequence::Delete, [this]
            { m_skeleton->deleteSelected(); }, "trash"));

        QMenu* model = addTabMenu("&Model");
        model->addAction(makeAction(this, "Add &Socket", QKeySequence("Ctrl+Alt+S"), [this]
            { m_skeleton->addSocket(); }, "plus"));
        model->addAction(makeAction(this, "Add &Collider", QKeySequence("Ctrl+Shift+C"), [this]
            { m_skeleton->addCollider(); }, "plus"));
        model->addAction(makeAction(this, QString::fromUtf8("Attach &Model…"), QKeySequence("Ctrl+Shift+M"), [this]
            { m_skeleton->addChildModel(); }, "plus"));
        model->addSeparator();
        model->addAction(makeAction(this, "&Frame Model", QKeySequence(), [this]
            { m_view->frameModel(); }));

        auto* gizmoGroup = new QActionGroup(this);
        QAction* moveAct = makeAction(this, "&Move Gizmo", QKeySequence(), [this]
            { m_view->setGizmoMode(ModelViewport::GizmoMode::Move); });
        QAction* rotateAct = makeAction(this, "&Rotate Gizmo", QKeySequence(), [this]
            { m_view->setGizmoMode(ModelViewport::GizmoMode::Rotate); });
        for (QAction* a : { moveAct, rotateAct })
        {
            a->setCheckable(true);
            gizmoGroup->addAction(a);
        }
        moveAct->setChecked(true);
        moveAct->setToolTip("Move gizmo (W in the viewport)");
        rotateAct->setToolTip("Rotate gizmo (E in the viewport)");
        connect(m_view, &ModelViewport::gizmoModeChanged, this, [moveAct, rotateAct](ModelViewport::GizmoMode mode)
            { (mode == ModelViewport::GizmoMode::Move ? moveAct : rotateAct)->setChecked(true); });
        model->addSeparator();
        model->addAction(moveAct);
        model->addAction(rotateAct);

        QMenu* view = addTabMenu("&View");
        addOverlayActions(view, this, m_view);
        view->addSeparator();
        for (QDockWidget* d : findChildren<QDockWidget*>(QString(), Qt::FindDirectChildrenOnly))
            view->addAction(d->toggleViewAction());

        auto* bar = addToolBar("Model");
        bar->setObjectName("ModelToolbar");
        bar->setMovable(false);
        bar->setIconSize(QSize(18, 18));
        bar->addAction(undo);
        bar->addAction(redo);
        bar->addSeparator();
        bar->addAction(moveAct);
        bar->addAction(rotateAct);
    }

    QString ModelEditorTab::filePath() const
    {
        return m_doc->path();
    }

    bool ModelEditorTab::isModified() const
    {
        return m_doc->isModified();
    }

    bool ModelEditorTab::openFile(const QString& path, QString* error)
    {
        if (!m_doc->load(path, error)) return false;
        updateContentRoot();
        const KModelData& model = m_doc->model();
        showStatus(QString("%1: %2 meshes, %3 bones, %4 sockets, %5 colliders")
                       .arg(QFileInfo(path).fileName())
                       .arg(model.meshes.size())
                       .arg(model.bones.size())
                       .arg(model.sockets.size())
                       .arg(model.colliders.size()),
            6000);
        emit titleChanged();
        return true;
    }

    bool ModelEditorTab::save()
    {
        if (!m_doc->isOpen()) return false;
        QString error;
        if (!m_doc->save(&error))
        {
            QMessageBox::warning(this, "Save Model", QString("Could not save the model:\n%1").arg(error));
            return false;
        }
        showStatus(QString("Saved %1").arg(QFileInfo(m_doc->path()).fileName()));
        emit fileSaved(m_doc->path());
        return true;
    }

    bool ModelEditorTab::saveAs()
    {
        if (!m_doc->isOpen()) return false;
        QString path = QFileDialog::getSaveFileName(this, "Save Model As", m_doc->path(), "KES models (*.kmodel)");
        if (path.isEmpty()) return false;
        if (!path.endsWith(".kmodel", Qt::CaseInsensitive)) path += ".kmodel";
        QString error;
        if (!m_doc->saveAs(path, &error))
        {
            QMessageBox::warning(this, "Save Model", QString("Could not save the model:\n%1").arg(error));
            return false;
        }
        emit titleChanged();
        emit fileSaved(m_doc->path());
        return true;
    }

    void ModelEditorTab::projectChanged()
    {
        updateContentRoot();
    }

    void ModelEditorTab::assetSaved(const QString&)
    {
        // child models and materials are reloaded by the viewport when their files change
        m_view->update();
    }

    void ModelEditorTab::updateContentRoot()
    {
        m_preview->setContentRoot(contentRootFor(context(), m_doc->path()));
        m_statusContent->setText(m_preview->contentRoot().isEmpty() ? QStringLiteral("No content folder found")
                                                                    : QString("Content: %1").arg(QDir::toNativeSeparators(m_preview->contentRoot())));
        rescanClips();
    }

    void ModelEditorTab::rescanClips()
    {
        m_animation->setClips(scanClips(m_preview, m_doc->isOpen() ? m_doc->path() : QString()));
    }

    // ---------------------------------------------------------------------------------------------
    // Animation graph tab

    AnimGraphTab::AnimGraphTab(EditorContext* ctx, QWidget* parent)
        : EditorTab(ctx, QStringLiteral("animGraph"), parent)
    {
        m_doc = new ModelDocument(this);
        m_graphDoc = new AnimGraphDocument(this);
        m_graph = m_graphDoc->root();
        // must exist before the viewport so the pose is updated before it redraws
        m_preview = new PreviewState(m_doc, m_graphDoc, this);
        m_preview->setContentRoot(ctx->projectDir());
        m_view = new ModelViewport(m_doc, m_preview);
        m_view->setEditable(false);

        auto* graphArea = new QWidget;
        auto* graphLay = new QVBoxLayout(graphArea);
        graphLay->setContentsMargins(0, 0, 0, 0);
        graphLay->setSpacing(0);
        m_breadcrumbs = new QWidget;
        auto* crumbs = new QHBoxLayout(m_breadcrumbs);
        crumbs->setContentsMargins(6, 3, 6, 3);
        crumbs->setSpacing(2);
        m_canvas = new QStackedWidget;
        graphLay->addWidget(m_breadcrumbs);
        graphLay->addWidget(m_canvas, 1);

        auto* split = new QSplitter(Qt::Horizontal);
        split->addWidget(graphArea);
        split->addWidget(m_view);
        split->setStretchFactor(0, 3);
        split->setStretchFactor(1, 2);
        setCentralWidget(split);

        m_libraries = new QStackedWidget;
        m_emptyLibrary = new QWidget;
        m_libraries->addWidget(m_emptyLibrary);
        m_inspectors = new QStackedWidget;
        addDock(this, "Node Library", "LibraryDock", m_libraries, Qt::LeftDockWidgetArea);
        QDockWidget* node = addDock(this, "Node", "NodeDock", m_inspectors, Qt::RightDockWidgetArea);
        m_params = new GraphParamsPanel(m_preview);
        QDockWidget* params = addDock(this, "Graph Parameters", "ParamsDock", scrolled(m_params), Qt::RightDockWidgetArea);
        splitDockWidget(node, params, Qt::Vertical);
        m_animation = new AnimationPanel(m_preview, true);
        addDock(this, "Animation", "AnimationDock", m_animation, Qt::BottomDockWidgetArea);

        m_statusModel = new QLabel;
        statusBar()->addPermanentWidget(m_statusModel);

        connect(m_graphDoc->undoStack(), &QUndoStack::cleanChanged, this, &EditorTab::titleChanged);
        connect(m_graph, &GraphModel::modelPathChanged, this, &AnimGraphTab::loadPreviewModel);
        connect(m_graphDoc, &AnimGraphDocument::graphAboutToBeRemoved, this, &AnimGraphTab::dropGraphEditor);
        connect(m_graphDoc, &AnimGraphDocument::machineAboutToBeRemoved, this, &AnimGraphTab::dropMachineEditor);
        connect(m_graphDoc, &AnimGraphDocument::structureChanged, this, &AnimGraphTab::validatePath);
        connect(m_graphDoc, &AnimGraphDocument::changed, this, &AnimGraphTab::rebuildBreadcrumbs);
        connect(m_doc, &ModelDocument::modelReset, this, [this]
            {
        for (nodegraph::GraphEditor* e : std::as_const(m_editors)) e->inspector()->scheduleRebuild(); });
        connect(m_preview, &PreviewState::poseChanged, this, &AnimGraphTab::updateWeights);
        connect(m_animation, &AnimationPanel::browseClipRequested, this, [this]
            {
        const QString path = QFileDialog::getOpenFileName(this, "Open Clip", lastDir(m_preview), "Animation clips (*.kanim)");
        if (path.isEmpty()) return;
        rememberDir(path);
        m_animation->selectClip(toContentPath(m_preview, path)); });

        m_path = { Layer{ m_graph, nullptr } };
        graphEditor(m_graph);
        buildActions();
        newGraph();
        m_preview->setMode(PreviewState::Mode::Graph);
        m_preview->setPlaying(true);
        restoreLayout();
    }

    AnimGraphTab::~AnimGraphTab()
    {
        m_graphDoc->disconnect(this);
        m_preview->disconnect(this);
    }

    nodegraph::GraphEditor* AnimGraphTab::graphEditor(GraphModel* graph)
    {
        if (nodegraph::GraphEditor* e = m_editors.value(graph)) return e;
        auto* e = new nodegraph::GraphEditor(graph, this);
        m_editors.insert(graph, e);
        e->inspector()->setParamEditorFactory(
            [this](int nodeId, const ParamDef& p, const QVariant& current, const std::function<void(const QVariant&)>& apply) -> QWidget*
            {
                if (p.kind == ParamKind::Clip) return clipEditor(nodeId, current.toString(), apply);
                if (p.kind != ParamKind::Bone) return nullptr;
                QStringList bones;
                for (const KModelNode& n : m_doc->model().nodes)
                    bones << QString::fromStdString(n.name);
                auto* combo = pathCombo(bones, current.toString(), "(whole model)");
                connect(combo, &QComboBox::currentIndexChanged, combo, [apply, combo](int)
                    { apply(combo->currentData().toString()); });
                return combo;
            });
        connect(e, &nodegraph::GraphEditor::statusMessage, this, [this](const QString& t)
            { showStatus(t); });
        connect(e->scene(), &nodegraph::NodeGraphScene::nodeDoubleClicked, this, [this, graph](int id)
            {
        if (m_graphDoc->machine(graph, id)) openMachine(graph, id); });
        if (m_snap) e->scene()->setSnapToGrid(m_snap->isChecked());
        m_canvas->addWidget(e->view());
        QWidget* scroll = scrolled(e->inspector());
        m_inspectorScrolls.insert(e, scroll);
        m_inspectors->addWidget(scroll);
        m_libraries->addWidget(e->library());
        return e;
    }

    StateMachineEditor* AnimGraphTab::machineEditor(StateMachineModel* machine)
    {
        if (StateMachineEditor* e = m_machineEditors.value(machine)) return e;
        auto* e = new StateMachineEditor(machine, this);
        m_machineEditors.insert(machine, e);
        connect(e, &StateMachineEditor::stateOpened, this, [this, machine](int id)
            { openState(machine, id); });
        connect(e, &StateMachineEditor::statusMessage, this, [this](const QString& t)
            { showStatus(t); });
        m_canvas->addWidget(e->view());
        QWidget* scroll = scrolled(e->inspector());
        m_inspectorScrolls.insert(e, scroll);
        m_inspectors->addWidget(scroll);
        return e;
    }

    void AnimGraphTab::dropGraphEditor(GraphModel* graph)
    {
        nodegraph::GraphEditor* e = m_editors.take(graph);
        if (!e) return;
        delete e->view();
        delete m_inspectorScrolls.take(e);
        delete e->library();
        delete e;
    }

    void AnimGraphTab::dropMachineEditor(StateMachineModel* machine)
    {
        StateMachineEditor* e = m_machineEditors.take(machine);
        if (!e) return;
        delete e->view();
        delete m_inspectorScrolls.take(e);
        delete e;
    }

    nodegraph::GraphEditor* AnimGraphTab::currentGraphEditor() const
    {
        const Layer& l = m_path.last();
        return l.machine ? nullptr : m_editors.value(l.graph.data());
    }

    StateMachineEditor* AnimGraphTab::currentMachineEditor() const
    {
        const Layer& l = m_path.last();
        return l.machine ? m_machineEditors.value(l.machine.data()) : nullptr;
    }

    void AnimGraphTab::openMachine(GraphModel* graph, int nodeId)
    {
        StateMachineModel* machine = m_graphDoc->machine(graph, nodeId);
        if (!machine) return;
        for (int i = 0; i < m_path.size(); ++i)
        {
            if (m_path[i].graph != graph || m_path[i].machine) continue;
            m_path.resize(i + 1);
            m_path.append(Layer{ nullptr, machine });
            showCurrentLayer();
            QTimer::singleShot(0, this, [this]
                {
                if (StateMachineEditor* e = currentMachineEditor()) e->view()->frameAll(); });
            return;
        }
    }

    void AnimGraphTab::openState(StateMachineModel* machine, int stateId)
    {
        const AnimState* state = machine->state(stateId);
        if (!state) return;
        for (int i = 0; i < m_path.size(); ++i)
        {
            if (m_path[i].machine != machine) continue;
            m_path.resize(i + 1);
            m_path.append(Layer{ state->graph, nullptr });
            showCurrentLayer();
            QTimer::singleShot(0, this, [this]
                {
                if (nodegraph::GraphEditor* e = currentGraphEditor()) e->view()->frameAll(); });
            return;
        }
    }

    void AnimGraphTab::goToDepth(int depth)
    {
        depth = std::clamp(depth, 0, int(m_path.size()) - 1);
        if (depth == m_path.size() - 1) return;
        m_path.resize(depth + 1);
        showCurrentLayer();
    }

    void AnimGraphTab::validatePath()
    {
        int keep = 1;
        while (keep < m_path.size() && (m_path[keep].graph || m_path[keep].machine))
            ++keep;
        m_path.resize(keep);
        showCurrentLayer();
    }

    void AnimGraphTab::showCurrentLayer()
    {
        const Layer& layer = m_path.last();
        if (layer.machine)
        {
            StateMachineEditor* e = machineEditor(layer.machine);
            m_canvas->setCurrentWidget(e->view());
            m_inspectors->setCurrentWidget(m_inspectorScrolls.value(e));
            m_libraries->setCurrentWidget(m_emptyLibrary);
        }
        else
        {
            nodegraph::GraphEditor* g = graphEditor(layer.graph);
            m_canvas->setCurrentWidget(g->view());
            m_inspectors->setCurrentWidget(m_inspectorScrolls.value(g));
            m_libraries->setCurrentWidget(g->library());
        }
        const bool graphLayer = !layer.machine;
        for (QAction* a : std::as_const(m_graphOnlyActions))
            a->setEnabled(graphLayer);
        rebuildBreadcrumbs();
        updateWeights();
    }

    void AnimGraphTab::rebuildBreadcrumbs()
    {
        auto* lay = static_cast<QHBoxLayout*>(m_breadcrumbs->layout());
        while (QLayoutItem* item = lay->takeAt(0))
        {
            if (QWidget* w = item->widget()) w->deleteLater();
            delete item;
        }
        m_breadcrumbs->setVisible(m_path.size() > 1);
        if (m_path.size() <= 1) return;

        for (int i = 0; i < m_path.size(); ++i)
        {
            QString text = QStringLiteral("Graph");
            if (StateMachineModel* m = m_path[i].machine)
                text = m->name();
            else if (i > 0)
            {
                int stateId = 0;
                if (StateMachineModel* owner = m_graphDoc->ownerOf(m_path[i].graph, &stateId)) text = owner->stateName(stateId);
            }
            if (i > 0)
            {
                auto* sep = new QLabel(QString::fromUtf8("›"));
                sep->setObjectName("Muted");
                lay->addWidget(sep);
            }
            auto* b = new QToolButton;
            b->setText(text);
            b->setAutoRaise(true);
            if (i == m_path.size() - 1)
            {
                QFont f = b->font();
                f.setBold(true);
                b->setFont(f);
                b->setEnabled(false);
            }
            else
                connect(b, &QToolButton::clicked, this, [this, i]
                    { goToDepth(i); });
            lay->addWidget(b);
        }
        lay->addStretch(1);
    }

    void AnimGraphTab::updateWeights()
    {
        const bool live = m_preview->mode() == PreviewState::Mode::Graph && m_preview->graphError().isEmpty();
        AnimBlendTreeInstance& inst = m_preview->graphInstance();
        if (StateMachineEditor* e = currentMachineEditor())
        {
            if (!live)
            {
                e->scene()->setStateWeights(nullptr);
                return;
            }
            const QHash<int, int> ids = m_preview->graphIds().states.value(e->machine());
            e->scene()->setStateWeights([&inst, ids](int id)
                {
                const auto it = ids.constFind(id);
                return it == ids.constEnd() ? 0.0f : inst.GetStateWeight(*it); });
        }
        else if (nodegraph::GraphEditor* g = currentGraphEditor())
        {
            if (!live)
            {
                g->scene()->setNodeWeights(nullptr);
                return;
            }
            const QHash<int, int> ids = m_preview->graphIds().nodes.value(m_path.last().graph.data());
            g->scene()->setNodeWeights([&inst, ids](int id)
                {
                const auto it = ids.constFind(id);
                return it == ids.constEnd() ? 0.0f : inst.GetNodeWeight(*it); });
        }
    }

    void AnimGraphTab::buildActions()
    {
        QAction* undo = m_graphDoc->undoStack()->createUndoAction(this, "&Undo");
        undo->setShortcut(QKeySequence::Undo);
        undo->setIcon(kes::style::icon("undo"));
        QAction* redo = m_graphDoc->undoStack()->createRedoAction(this, "&Redo");
        redo->setShortcuts({ QKeySequence::Redo, QKeySequence("Ctrl+Y") });
        redo->setIcon(kes::style::icon("redo"));

        const auto onGraph = [this](std::function<void(nodegraph::GraphEditor*)> fn)
        {
            return [this, fn]
            {
                if (nodegraph::GraphEditor* e = currentGraphEditor()) fn(e);
            };
        };
        const auto onView = [this](std::function<void(nodegraph::NodeGraphView*)> fn)
        {
            return [this, fn]
            {
                if (nodegraph::GraphEditor* e = currentGraphEditor())
                    fn(e->view());
                else if (StateMachineEditor* m = currentMachineEditor())
                    fn(m->view());
            };
        };

        QAction* cut = makeAction(this, "Cu&t", QKeySequence::Cut, onGraph([](auto* e)
            { e->scene()->cutSelection(); }));
        QAction* copy = makeAction(this, "&Copy", QKeySequence::Copy, onGraph([](auto* e)
            { e->scene()->copySelection(); }), "copy");
        QAction* paste = makeAction(this, "&Paste", QKeySequence::Paste, onGraph([](auto* e)
            { e->scene()->paste(e->view()->cursorScenePos()); }));
        QAction* duplicate = makeAction(this, "&Duplicate", QKeySequence("Ctrl+D"), onGraph([](auto* e)
            { e->scene()->duplicateSelection(); }));
        QAction* remove = makeAction(this, "De&lete", QKeySequence::Delete, [this]
            {
            if (nodegraph::GraphEditor* e = currentGraphEditor()) e->scene()->deleteSelection();
            else if (StateMachineEditor* m = currentMachineEditor()) m->scene()->deleteSelection(); }, "trash");
        QAction* selectAll = makeAction(this, "Select &All", QKeySequence::SelectAll, [this]
            {
            if (nodegraph::GraphEditor* e = currentGraphEditor()) e->scene()->selectNodes(e->model()->nodeIds());
            else if (StateMachineEditor* m = currentMachineEditor()) m->scene()->selectAllStates(); });
        QAction* addNode = makeAction(this, QString::fromUtf8("&Add Node / State…"), QKeySequence("Shift+A"), [this]
            {
            if (nodegraph::GraphEditor* e = currentGraphEditor()) e->showAddNodePopup();
            else if (StateMachineEditor* m = currentMachineEditor()) m->scene()->addStateAt(m->view()->cursorScenePos()); }, "plus");
        QAction* frameAll = makeAction(this, "Frame &All", QKeySequence(Qt::Key_F), onView([](auto* v)
            { v->frameAll(); }));
        QAction* frameSelection = makeAction(this, "Frame &Selection", QKeySequence("Shift+F"), onView([](auto* v)
            { v->frameSelection(); }));
        QAction* zoomIn = makeAction(this, "Zoom &In", QKeySequence::ZoomIn, onView([](auto* v)
            { v->zoomBy(1.2); }));
        QAction* zoomOut = makeAction(this, "Zoom &Out", QKeySequence::ZoomOut, onView([](auto* v)
            { v->zoomBy(1 / 1.2); }));
        QAction* zoomReset = makeAction(this, "&Reset Zoom", QKeySequence("Ctrl+0"), onView([](auto* v)
            { v->resetZoom(); }));
        QAction* up = makeAction(this, "&Up One Level", QKeySequence("Alt+Up"), [this]
            { goToDepth(int(m_path.size()) - 2); });
        m_snap = new QAction("Snap to &Grid", this);
        m_snap->setCheckable(true);
        connect(m_snap, &QAction::toggled, this, [this](bool on)
            {
            for (nodegraph::GraphEditor* e : std::as_const(m_editors)) e->scene()->setSnapToGrid(on); });
        m_graphOnlyActions = { cut, copy, paste, duplicate, m_snap };

        // F and Delete belong to whichever view has focus; the model viewport frames itself on F
        for (QAction* a : { frameAll, remove })
        {
            a->setShortcutContext(Qt::WidgetWithChildrenShortcut);
            m_canvas->addAction(a);
        }

        QMenu* edit = addTabMenu("&Edit");
        edit->addActions({ undo, redo });
        edit->addSeparator();
        edit->addActions({ cut, copy, paste, duplicate, remove });
        edit->addSeparator();
        edit->addActions({ selectAll, addNode });

        QMenu* graph = addTabMenu("&Graph");
        graph->addAction(makeAction(this, "&Restart Preview", QKeySequence("Ctrl+R"), [this]
            { m_preview->restartGraph(); }));
        graph->addAction(up);
        graph->addSeparator();
        graph->addAction(makeAction(this, QString::fromUtf8("Set Preview &Model…"), QKeySequence(), [this]
            { choosePreviewModel(); }, "open"));
        graph->addAction(makeAction(this, "&Edit Preview Model", QKeySequence(), [this]
            {
        if (m_doc->isOpen()) emit openRequested(m_doc->path());
        else showStatus("The graph has no preview model yet."); }));

        QMenu* view = addTabMenu("&View");
        view->addActions({ frameAll, frameSelection, zoomIn, zoomOut, zoomReset });
        view->addSeparator();
        view->addAction(m_snap);
        view->addSeparator();
        QMenu* overlays = view->addMenu("&Preview Overlays");
        addOverlayActions(overlays, this, m_view);
        view->addSeparator();
        for (QDockWidget* d : findChildren<QDockWidget*>(QString(), Qt::FindDirectChildrenOnly))
            view->addAction(d->toggleViewAction());

        auto* bar = addToolBar("Animation Graph");
        bar->setObjectName("AnimGraphToolbar");
        bar->setMovable(false);
        bar->setIconSize(QSize(18, 18));
        bar->addAction(undo);
        bar->addAction(redo);
        bar->addSeparator();
        bar->addAction(addNode);
        bar->addAction(frameAll);
    }

    QWidget* AnimGraphTab::clipEditor(int, const QString& current, const std::function<void(const QVariant&)>& apply)
    {
        auto* row = new QWidget;
        auto* h = new QHBoxLayout(row);
        h->setContentsMargins(0, 0, 0, 0);
        h->setSpacing(3);
        auto* combo = pathCombo(m_clips, current, "(no clip)");
        auto* browse = new QToolButton;
        browse->setIcon(kes::style::icon("open"));
        browse->setToolTip("Pick a .kanim file");
        h->addWidget(combo, 1);
        h->addWidget(browse);
        connect(combo, &QComboBox::currentIndexChanged, row, [apply, combo](int)
            { apply(combo->currentData().toString()); });
        connect(browse, &QToolButton::clicked, row, [this, apply]
            {
        const QString path = QFileDialog::getOpenFileName(this, "Pick Clip", lastDir(m_preview), "Animation clips (*.kanim)");
        if (path.isEmpty()) return;
        rememberDir(path);
        const QString rel = toContentPath(m_preview, path);
        if (QDir::isAbsolutePath(rel)) showStatus("The clip is outside the content folder; the game will not find it.", 8000);
        apply(rel); });
        return row;
    }

    QString AnimGraphTab::displayName() const
    {
        return m_graphPath.isEmpty() ? QStringLiteral("Untitled Animation Graph") : QFileInfo(m_graphPath).fileName();
    }

    bool AnimGraphTab::isModified() const
    {
        return !m_graphDoc->undoStack()->isClean();
    }

    void AnimGraphTab::newGraph()
    {
        m_graphDoc->newDocument();
        m_path = { Layer{ m_graph, nullptr } };
        showCurrentLayer();
        m_graphPath.clear();
        m_preview->restartGraph();
        QTimer::singleShot(0, this, [this]
            { graphEditor(m_graph)->view()->frameAll(); });
        emit titleChanged();
    }

    bool AnimGraphTab::openFile(const QString& path, QString* error)
    {
        AnimBlendTreeData tree;
        std::string err;
        if (!LoadAnimBlendTreeFile(QFileInfo(path).absoluteFilePath().toUtf8().constData(), tree, &err))
        {
            if (error) *error = QString("Could not open %1:\n%2").arg(QDir::toNativeSeparators(path), QString::fromStdString(err));
            return false;
        }
        m_graphPath = QFileInfo(path).absoluteFilePath();
        m_preview->setContentRoot(contentRootFor(context(), m_graphPath));
        QStringList warnings;
        m_graphDoc->loadBlendTree(tree, &warnings);
        m_path = { Layer{ m_graph, nullptr } };
        showCurrentLayer();
        loadPreviewModel();
        if (!m_graph->modelPath().isEmpty() && !m_doc->isOpen())
            warnings << QString("The graph's model %1 was not found.").arg(m_graph->modelPath());
        if (!warnings.isEmpty()) QMessageBox::information(this, "Open Graph", warnings.join("\n"));

        m_preview->restartGraph();
        QTimer::singleShot(0, this, [this]
            { graphEditor(m_graph)->view()->frameAll(); });
        emit titleChanged();
        return true;
    }

    bool AnimGraphTab::save()
    {
        if (m_graphPath.isEmpty()) return saveAs();
        std::string error;
        const AnimBlendTreeData tree = m_graphDoc->toBlendTree();
        if (!SaveAnimBlendTreeFile(m_graphPath.toUtf8().constData(), tree, &error))
        {
            QMessageBox::warning(this, "Save Graph", QString("Could not save the graph:\n%1").arg(QString::fromStdString(error)));
            return false;
        }
        m_graphDoc->undoStack()->setClean();
        showStatus(QString("Saved %1").arg(QFileInfo(m_graphPath).fileName()));
        AnimBlendTreeInstance check;
        std::string buildError;
        if (!check.Build(tree, &buildError))
            showStatus(QString("Saved, but the game will not play it yet: %1").arg(QString::fromStdString(buildError)), 8000);
        emit titleChanged();
        emit fileSaved(m_graphPath);
        return true;
    }

    bool AnimGraphTab::saveAs()
    {
        QString suggestion = m_graphPath;
        if (suggestion.isEmpty())
        {
            const QString dir = m_doc->isOpen() ? QFileInfo(m_doc->path()).absolutePath() : lastDir(m_preview);
            const QString stem = m_doc->isOpen() ? QFileInfo(m_doc->path()).completeBaseName() : QStringLiteral("animation");
            suggestion = QDir(dir).absoluteFilePath(stem + ".ag");
        }
        QString path = QFileDialog::getSaveFileName(this, "Save Animation Graph", suggestion, "Animation graphs (*.ag)");
        if (path.isEmpty()) return false;
        if (!path.endsWith(".ag", Qt::CaseInsensitive)) path += ".ag";
        m_graphPath = QFileInfo(path).absoluteFilePath();
        rememberDir(path);
        return save();
    }

    void AnimGraphTab::projectChanged()
    {
        m_preview->setContentRoot(contentRootFor(context(), m_graphPath));
        loadPreviewModel();
    }

    void AnimGraphTab::assetSaved(const QString& path)
    {
        if (!m_doc->isOpen() || QFileInfo(path).absoluteFilePath().compare(m_doc->path(), Qt::CaseInsensitive) != 0) return;
        QString error;
        if (m_doc->load(m_doc->path(), &error)) m_preview->restartGraph();
    }

    void AnimGraphTab::choosePreviewModel()
    {
        const QString start = m_doc->isOpen() ? QFileInfo(m_doc->path()).absolutePath() : lastDir(m_preview);
        const QString path = QFileDialog::getOpenFileName(this, "Preview Model", start, "KES models (*.kmodel)");
        if (path.isEmpty()) return;
        rememberDir(path);
        const QString rel = toContentPath(m_preview, path);
        m_graph->edit("Set graph model", [&]
            { m_graph->setModelPath(rel); });
    }

    void AnimGraphTab::loadPreviewModel()
    {
        const QString file = m_graph->modelPath().isEmpty() ? QString() : m_preview->resolvePath(m_graph->modelPath());
        if (file.isEmpty() || !QFileInfo::exists(file))
        {
            if (m_doc->isOpen()) m_doc->close();
            m_statusModel->setText(m_graph->modelPath().isEmpty() ? QStringLiteral("No preview model (Graph > Set Preview Model)")
                                                                  : QString("Preview model missing: %1").arg(m_graph->modelPath()));
        }
        else if (QFileInfo(file).absoluteFilePath().compare(m_doc->path(), Qt::CaseInsensitive) != 0)
        {
            QString error;
            if (m_doc->load(file, &error))
                m_statusModel->setText(QString("Preview: %1").arg(m_graph->modelPath()));
            else
                m_statusModel->setText(QString("Preview model failed to load: %1").arg(error));
        }
        rescanClips();
        m_view->frameModel();
    }

    void AnimGraphTab::rescanClips()
    {
        m_clips = scanClips(m_preview, m_doc->isOpen() ? m_doc->path() : QString());
        m_animation->setClips(m_clips);
        for (nodegraph::GraphEditor* e : std::as_const(m_editors))
            e->inspector()->scheduleRebuild();
    }

} // namespace kme
