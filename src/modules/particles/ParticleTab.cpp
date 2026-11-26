#include "ParticleTab.h"

#include "ParticleDocument.h"
#include "ParticleGraph.h"
#include "ParticleViewport.h"
#include "ValueEditors.h"

#include "core/EditorContext.h"
#include "framework/ContentPaths.h"
#include "framework/graph/GraphEditor.h"
#include "framework/graph/InspectorWidget.h"
#include "framework/graph/NodeGraphView.h"
#include "ui/KesStyle.h"

#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QSplitter>
#include <QStackedWidget>
#include <QTimer>
#include <QStatusBar>
#include <QToolBar>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <map>

namespace kpe
{
    namespace
    {
        constexpr int kRoleKind = Qt::UserRole;
        constexpr int kRoleEmitter = Qt::UserRole + 1;
        constexpr int kRoleIndex = Qt::UserRole + 2;
        constexpr int kRendererStage = 3;

        const char* kStageTitles[] = { "Emitter Update", "Particle Spawn", "Particle Update", "Render" };

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
            if (!ks.isEmpty())
                a->setShortcut(ks);
            if (!icon.isEmpty())
                a->setIcon(kes::style::icon(icon));
            if (slot)
                QObject::connect(a, &QAction::triggered, parent, std::move(slot));
            return a;
        }

        QDoubleSpinBox* doubleSpin(double value, double min, double max, double step)
        {
            auto* s = new QDoubleSpinBox;
            s->setRange(min, max);
            s->setDecimals(3);
            s->setSingleStep(step);
            s->setValue(value);
            s->setKeyboardTracking(false);
            return s;
        }

        QSpinBox* intSpin(int value, int min, int max)
        {
            auto* s = new QSpinBox;
            s->setRange(min, max);
            s->setValue(value);
            s->setKeyboardTracking(false);
            return s;
        }

        QLabel* note(const QString& text)
        {
            auto* l = new QLabel(text);
            l->setWordWrap(true);
            l->setObjectName("Muted");
            return l;
        }

        // The module list of one stage in stack order, as indices into emitter.modules.
        std::vector<int> stageModules(const ParticleEmitterData& e, int stage)
        {
            std::vector<int> out;
            for (int i = 0; i < int(e.modules.size()); ++i)
                if (int(e.modules[size_t(i)].stage) == stage)
                    out.push_back(i);
            return out;
        }

        const ParticleModuleDef* defOf(const ParticleModuleData& m, bool renderer)
        {
            return renderer ? FindParticleRendererDef(m.type) : FindParticleModuleDef(m.type);
        }

        QString titleOf(const ParticleModuleData& m, bool renderer)
        {
            const ParticleModuleDef* def = defOf(m, renderer);
            return def ? QString::fromStdString(def->title) : QString::fromStdString(m.type) + " (unknown)";
        }
    }

    ParticleTab::ParticleTab(EditorContext* ctx, QWidget* parent)
        : EditorTab(ctx, QStringLiteral("particles"), parent)
    {
        m_doc = new ParticleDocument(this);
        m_view = new ParticleViewport;
        m_view->setResolver([this](const QString& p) { return resolve(p); });

        // Graph panel under the preview: library | canvas | node inspector, or a hint while no graph is open.
        m_graph = new nodegraph::GraphModel(&ParticleGraphDomain::instance(), this);
        m_graphEditor = new nodegraph::GraphEditor(m_graph, this);
        auto* graphPanel = new QWidget;
        auto* graphLayout = new QVBoxLayout(graphPanel);
        graphLayout->setContentsMargins(0, 0, 0, 0);
        graphLayout->setSpacing(0);
        m_graphLabel = new QLabel;
        m_graphLabel->setContentsMargins(8, 4, 8, 4);
        m_graphLabel->setStyleSheet(QStringLiteral("font-weight: 600; background: %1; border-bottom: 1px solid %2;").arg(kes::style::panel().name(), kes::style::border().name()));
        graphLayout->addWidget(m_graphLabel);
        m_graphStack = new QStackedWidget;
        auto* hint = new QLabel("No graph open.");
        hint->setAlignment(Qt::AlignCenter);
        hint->setWordWrap(true);
        hint->setObjectName("EmptyState");
        m_graphStack->addWidget(hint);
        graphLayout->addWidget(m_graphStack, 1);
        auto* graphSplit = new QSplitter(Qt::Horizontal);
        graphSplit->addWidget(m_graphEditor->library());
        graphSplit->addWidget(m_graphEditor->view());
        auto* inspectorScroll = new QScrollArea;
        inspectorScroll->setWidgetResizable(true);
        inspectorScroll->setWidget(m_graphEditor->inspector());
        graphSplit->addWidget(inspectorScroll);
        graphSplit->setStretchFactor(0, 1);
        graphSplit->setStretchFactor(1, 4);
        graphSplit->setStretchFactor(2, 1);
        m_graphStack->addWidget(graphSplit);

        m_centralSplit = new QSplitter(Qt::Vertical);
        m_centralSplit->addWidget(m_view);
        m_centralSplit->addWidget(graphPanel);
        m_centralSplit->setStretchFactor(0, 3);
        m_centralSplit->setStretchFactor(1, 2);
        m_centralSplit->setChildrenCollapsible(false);
        setCentralWidget(m_centralSplit);

        // Stack
        auto* stackPanel = new QWidget;
        auto* stackLayout = new QVBoxLayout(stackPanel);
        stackLayout->setContentsMargins(0, 0, 0, 0);
        auto* stackBar = new QToolBar;
        stackBar->setIconSize(QSize(16, 16));
        stackLayout->addWidget(stackBar);
        m_tree = new QTreeWidget;
        m_tree->setHeaderHidden(true);
        m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
        m_tree->setIndentation(14);
        stackLayout->addWidget(m_tree, 1);
        addDock(this, "System", "ParticleStackDock", stackPanel, Qt::LeftDockWidgetArea);

        m_details = new QScrollArea;
        m_details->setWidgetResizable(true);
        addDock(this, "Details", "ParticleDetailsDock", m_details, Qt::RightDockWidgetArea);

        m_stats = new QLabel;
        statusBar()->addPermanentWidget(m_stats);

        // Stack toolbar
        auto* addEmitterButton = new QToolButton;
        addEmitterButton->setText("Emitter");
        addEmitterButton->setIcon(kes::style::icon("plus"));
        addEmitterButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        addEmitterButton->setPopupMode(QToolButton::InstantPopup);
        auto* templates = new QMenu(addEmitterButton);
        for (const QString& name : emitterTemplateNames())
            templates->addAction(name, this, [this, name] { addEmitter(name); });
        addEmitterButton->setMenu(templates);
        stackBar->addWidget(addEmitterButton);
        auto* addModuleButton = new QToolButton;
        addModuleButton->setText("Module");
        addModuleButton->setIcon(kes::style::icon("plus"));
        addModuleButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        addModuleButton->setToolTip("Add a module to the selected stage (or a renderer under Render)");
        connect(addModuleButton, &QToolButton::clicked, this, [this, addModuleButton]
            { showContextMenu(m_tree->viewport()->mapFromGlobal(addModuleButton->mapToGlobal(QPoint(0, addModuleButton->height())))); });
        stackBar->addWidget(addModuleButton);
        stackBar->addSeparator();
        stackBar->addAction(makeAction(this, "Move Up", {}, [this] { moveSelected(-1); }, "up"));
        stackBar->addAction(makeAction(this, "Move Down", {}, [this] { moveSelected(1); }, "down"));
        stackBar->addAction(makeAction(this, "Duplicate", {}, [this] { duplicateSelected(); }, "copy"));
        stackBar->addAction(makeAction(this, "Delete", {}, [this] { removeSelected(); }, "trash"));

        connect(m_tree, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem* item)
            {
                if (m_rebuildingTree)
                    return;
                m_selection = selectionOf(item);
                rebuildDetails();
                openSelectionGraph();
            });
        connect(m_tree, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem* item, int)
            {
                if (m_rebuildingTree)
                    return;
                const Selection s = selectionOf(item);
                const bool on = item->checkState(0) == Qt::Checked;
                if (s.kind == Kind::Emitter && item->parent() == nullptr)
                {
                    m_doc->edit(on ? "Enable emitter" : "Disable emitter",
                        [s, on](ParticleSystemData& d) { d.emitters[size_t(s.emitter)].enabled = on; }, false);
                }
                else if (s.kind == Kind::Module || s.kind == Kind::Renderer)
                {
                    const bool renderer = s.kind == Kind::Renderer;
                    m_doc->edit(on ? "Enable module" : "Disable module", [s, on, renderer](ParticleSystemData& d)
                        {
                            ParticleEmitterData& e = d.emitters[size_t(s.emitter)];
                            (renderer ? e.renderers : e.modules)[size_t(s.index)].enabled = on;
                        },
                        false);
                }
            });
        connect(m_tree, &QTreeWidget::customContextMenuRequested, this, &ParticleTab::showContextMenu);
        connect(m_tree, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem* item)
            {
                const Selection s = selectionOf(item);
                if (s.kind != Kind::Emitter || item->parent())
                    return;
                bool ok = false;
                const QString name = QInputDialog::getText(this, "Rename Emitter", "Name:", QLineEdit::Normal,
                    QString::fromStdString(m_doc->data().emitters[size_t(s.emitter)].name), &ok);
                if (ok && !name.trimmed().isEmpty())
                    m_doc->edit("Rename emitter", [s, name](ParticleSystemData& d) { d.emitters[size_t(s.emitter)].name = name.trimmed().toStdString(); }, true);
            });

        connect(m_doc, &ParticleDocument::changed, this, &ParticleTab::onDocumentChanged);
        connect(m_doc->undoStack(), &QUndoStack::cleanChanged, this, &EditorTab::titleChanged);
        connect(m_doc, &ParticleDocument::pathChanged, this, &EditorTab::titleChanged);
        connect(m_graph, &nodegraph::GraphModel::changed, this, [this]
            {
                if (m_loadingGraph || m_graphEmitter < 0)
                    return;
                const int emitter = m_graphEmitter;
                const int id = m_graphId;
                ParticleGraphData updated;
                updated.id = id;
                graphFromModel(*m_graph, updated);
                m_doc->edit("Edit graph", [emitter, id, updated](ParticleSystemData& d)
                    {
                        if (emitter >= int(d.emitters.size()))
                            return;
                        for (ParticleGraphData& g : d.emitters[size_t(emitter)].graphs)
                            if (g.id == id)
                                g = updated;
                    },
                    false);
            });
        connect(m_graphEditor, &nodegraph::GraphEditor::statusMessage, this, [this](const QString& t) { showStatus(t); });
        connect(m_view, &ParticleViewport::statsChanged, this, [this](int particles, int lights, double ms)
            { m_stats->setText(QString("%1 particles   %2 lights   %3 ms sim").arg(particles).arg(lights).arg(ms, 0, 'f', 2)); });
        connect(m_view, &ParticleViewport::errorsChanged, this, [this](const QStringList& errors)
            {
                ProblemList problems;
                for (const QString& e : errors)
                    problems << Problem{ ProblemSeverity::Error, e, {}, {} };
                setProblems(problems);
            });

        buildActions();
        updateContentRoot(ctx->projectDir());
        m_doc->reset(makeDefaultSystem());
        m_view->setSystem(&m_doc->data());
        restoreLayout();
    }

    ParticleTab::~ParticleTab()
    {
        // Children are destroyed after this destructor; the undo stack emits
        // cleanChanged while it is cleared, which must not reach this tab any more.
        m_doc->undoStack()->disconnect(this);
        m_doc->disconnect(this);
        m_view->disconnect(this);
        m_graph->disconnect(this);
        m_graphEditor->disconnect(this);
        m_tree->disconnect(this);
    }

    void ParticleTab::buildActions()
    {
        QAction* undo = m_doc->undoStack()->createUndoAction(this, "&Undo");
        undo->setShortcut(QKeySequence::Undo);
        undo->setIcon(kes::style::icon("undo"));
        QAction* redo = m_doc->undoStack()->createRedoAction(this, "&Redo");
        redo->setShortcuts({ QKeySequence::Redo, QKeySequence("Ctrl+Y") });
        redo->setIcon(kes::style::icon("redo"));

        QAction* remove = makeAction(this, "Delete Module", QKeySequence::Delete, [this] { removeSelected(); }, "trash");
        remove->setShortcutContext(Qt::WidgetWithChildrenShortcut);
        m_tree->addAction(remove);

        QMenu* edit = addTabMenu("&Edit");
        edit->addActions({ undo, redo });
        edit->addSeparator();
        edit->addAction(remove);
        edit->addAction(makeAction(this, "Duplicate", QKeySequence("Ctrl+D"), [this] { duplicateSelected(); }, "copy"));
        edit->addAction(makeAction(this, "Remove Unused Graphs", {}, [this]
            {
                m_doc->edit("Remove unused graphs", [](ParticleSystemData& d)
                    {
                        for (ParticleEmitterData& e : d.emitters)
                        {
                            std::vector<int> used;
                            auto collect = [&used](const std::vector<ParticleModuleData>& list)
                            {
                                for (const ParticleModuleData& m : list)
                                    for (const ParticleParam& p : m.params)
                                        if (p.value.mode == ParticleValueMode::Graph)
                                            used.push_back(p.value.graph);
                            };
                            collect(e.modules);
                            collect(e.renderers);
                            e.graphs.erase(std::remove_if(e.graphs.begin(), e.graphs.end(),
                                               [&used](const ParticleGraphData& g) { return std::find(used.begin(), used.end(), g.id) == used.end(); }),
                                e.graphs.end());
                        }
                    },
                    true);
            }));

        // Graph actions only fire while the graph canvas has focus.
        const nodegraph::GraphEditor::Actions g = m_graphEditor->createActions(this);
        QMenu* graph = addTabMenu("&Graph");
        for (QAction* a : { g.addNode, g.cut, g.copy, g.paste, g.duplicate, g.remove, g.selectAll, g.frameAll, g.frameSelection })
        {
            a->setShortcutContext(Qt::WidgetWithChildrenShortcut);
            m_graphEditor->view()->addAction(a);
            graph->addAction(a);
        }

        // Preview toolbar
        QToolBar* bar = addToolBar("Preview");
        bar->setObjectName("ParticlePreviewBar");
        QAction* play = makeAction(this, "Pause", {}, {});
        play->setCheckable(true);
        play->setToolTip("Play / pause the simulation (Space in the viewport)");
        connect(play, &QAction::toggled, this, [this, play](bool paused)
            {
                m_view->setPlaying(!paused);
                play->setText(paused ? "Play" : "Pause");
            });
        bar->addAction(play);
        bar->addAction(makeAction(this, "Restart", QKeySequence("Ctrl+R"), [this] { m_view->restart(); }));
        auto* speed = new QComboBox;
        for (const char* s : { "0.1x", "0.25x", "0.5x", "1x", "2x" })
            speed->addItem(s);
        speed->setCurrentIndex(3);
        speed->setToolTip("Simulation speed");
        connect(speed, &QComboBox::currentIndexChanged, this, [this](int i)
            {
                const float speeds[] = { 0.1f, 0.25f, 0.5f, 1.0f, 2.0f };
                m_view->setSpeed(speeds[i]);
            });
        bar->addWidget(speed);
        bar->addSeparator();
        auto toggle = [this, bar](const QString& text, bool on, const QString& tip, std::function<void(bool)> fn)
        {
            QAction* a = bar->addAction(text);
            a->setCheckable(true);
            a->setChecked(on);
            a->setToolTip(tip);
            connect(a, &QAction::toggled, this, std::move(fn));
        };
        toggle("Loop", true, "Restart the preview when the system finishes", [this](bool on) { m_view->setLooping(on); });
        toggle("Move", false, "Move the system in a circle (trails, spawn per unit, inherit velocity)", [this](bool on) { m_view->setMoving(on); });
        toggle("Floor", true, "Show the floor the particle lights shine on", [this](bool on) { m_view->setShowFloor(on); });
        bar->addAction(makeAction(this, "Frame", {}, [this] { m_view->frameParticles(); }));
        auto* lighting = new QComboBox;
        lighting->addItems({ "Day", "Dusk", "Night" });
        lighting->setToolTip("Preview lighting. Day is close to the game's default ambient; Night shows particle lights best.");
        connect(lighting, &QComboBox::currentIndexChanged, this, [this](int i) { m_view->setLightingPreset(i); });
        bar->addWidget(lighting);

        QMenu* view = addTabMenu("&View");
        for (QDockWidget* dock : findChildren<QDockWidget*>())
            view->addAction(dock->toggleViewAction());
    }

    QString ParticleTab::filePath() const
    {
        return m_doc->path();
    }

    bool ParticleTab::isModified() const
    {
        return m_doc->isModified();
    }

    bool ParticleTab::openFile(const QString& path, QString* error)
    {
        if (!m_doc->load(path, error))
            return false;
        updateContentRoot(path);
        closeGraph();
        m_view->restart();
        return true;
    }

    bool ParticleTab::save()
    {
        return m_doc->path().isEmpty() ? saveAs() : saveTo(m_doc->path());
    }

    bool ParticleTab::saveAs()
    {
        QString suggested = m_doc->path();
        if (suggested.isEmpty())
        {
            const QString dir = m_contentRoot.isEmpty() ? QString() : QDir(m_contentRoot).absoluteFilePath("particles");
            suggested = QDir(dir).absoluteFilePath("new_effect.kfx");
        }
        QString path = QFileDialog::getSaveFileName(this, "Save Particle System", suggested, "Particle systems (*.kfx)");
        if (path.isEmpty())
            return false;
        if (QFileInfo(path).suffix().isEmpty())
            path += ".kfx";
        return saveTo(path);
    }

    bool ParticleTab::saveTo(const QString& path)
    {
        QDir().mkpath(QFileInfo(path).absolutePath());
        QString error;
        if (!m_doc->saveAs(path, &error))
        {
            QMessageBox::warning(this, "Save Failed", QString("Could not save %1:\n%2").arg(path, error));
            return false;
        }
        updateContentRoot(path);
        showStatus(QString("Saved %1").arg(QFileInfo(path).fileName()));
        emit fileSaved(path);
        return true;
    }

    void ParticleTab::projectChanged()
    {
        if (m_doc->path().isEmpty())
            updateContentRoot(context()->projectDir());
    }

    void ParticleTab::assetSaved(const QString& path)
    {
        const QString suffix = QFileInfo(path).suffix().toLower();
        if (suffix == "mat" || suffix == "ktex" || suffix == "kmodel" || suffix == "png")
            m_view->clearAssetCache();
    }

    void ParticleTab::updateContentRoot(const QString& hint)
    {
        QString root = hint.isEmpty() ? QString() : forge::findContentRoot(hint);
        if (root.isEmpty())
            root = context()->projectDir();
        if (root != m_contentRoot)
        {
            m_contentRoot = root;
            m_view->clearAssetCache();
        }
    }

    QString ParticleTab::resolve(const QString& contentPath) const
    {
        if (contentPath.isEmpty())
            return {};
        if (QDir::isAbsolutePath(contentPath))
            return contentPath;
        return m_contentRoot.isEmpty() ? contentPath : QDir(m_contentRoot).absoluteFilePath(contentPath);
    }

    void ParticleTab::onDocumentChanged(bool structural)
    {
        m_view->restart();
        if (structural)
        {
            rebuildTree();
            rebuildDetails();
            reloadGraph();
        }
    }

    // Stack tree

    ParticleTab::Selection ParticleTab::selectionOf(QTreeWidgetItem* item) const
    {
        Selection s;
        if (!item)
            return s;
        s.kind = Kind(item->data(0, kRoleKind).toInt());
        s.emitter = item->data(0, kRoleEmitter).toInt();
        s.index = item->data(0, kRoleIndex).toInt();
        return s;
    }

    ParticleTab::Selection ParticleTab::currentSelection() const
    {
        return selectionOf(m_tree->currentItem());
    }

    void ParticleTab::rebuildTree()
    {
        m_rebuildingTree = true;
        m_tree->clear();
        const ParticleSystemData& data = m_doc->data();
        auto makeItem = [](QTreeWidgetItem* parent, const QString& text, Kind kind, int emitter, int index)
        {
            auto* item = new QTreeWidgetItem(parent);
            item->setText(0, text);
            item->setData(0, kRoleKind, int(kind));
            item->setData(0, kRoleEmitter, emitter);
            item->setData(0, kRoleIndex, index);
            return item;
        };
        QTreeWidgetItem* toSelect = nullptr;
        auto remember = [&](QTreeWidgetItem* item)
        {
            if (selectionOf(item) == m_selection)
                toSelect = item;
        };

        auto* system = makeItem(nullptr, "System", Kind::System, -1, -1);
        QFont bold = system->font(0);
        bold.setBold(true);
        system->setFont(0, bold);
        m_tree->addTopLevelItem(system);
        remember(system);

        for (int ei = 0; ei < int(data.emitters.size()); ++ei)
        {
            const ParticleEmitterData& e = data.emitters[size_t(ei)];
            auto* emitterItem = makeItem(nullptr, QString::fromStdString(e.name), Kind::Emitter, ei, -1);
            emitterItem->setFont(0, bold);
            emitterItem->setFlags(emitterItem->flags() | Qt::ItemIsUserCheckable);
            emitterItem->setCheckState(0, e.enabled ? Qt::Checked : Qt::Unchecked);
            emitterItem->setToolTip(0, "Double-click to rename");
            m_tree->addTopLevelItem(emitterItem);
            remember(emitterItem);
            auto* props = makeItem(emitterItem, "Properties", Kind::Emitter, ei, 0);
            remember(props);
            for (int stage = 0; stage < 4; ++stage)
            {
                auto* group = makeItem(emitterItem, kStageTitles[stage], Kind::Stage, ei, stage);
                group->setForeground(0, QColor(150, 170, 200));
                remember(group);
                if (stage == kRendererStage)
                {
                    for (int ri = 0; ri < int(e.renderers.size()); ++ri)
                    {
                        const ParticleRendererData& r = e.renderers[size_t(ri)];
                        auto* item = makeItem(group, titleOf(r, true), Kind::Renderer, ei, ri);
                        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
                        item->setCheckState(0, r.enabled ? Qt::Checked : Qt::Unchecked);
                        if (r.type == "Light")
                            item->setForeground(0, QColor(255, 214, 120));
                        remember(item);
                    }
                }
                else
                {
                    for (int mi : stageModules(e, stage))
                    {
                        const ParticleModuleData& m = e.modules[size_t(mi)];
                        auto* item = makeItem(group, titleOf(m, false), Kind::Module, ei, mi);
                        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
                        item->setCheckState(0, m.enabled ? Qt::Checked : Qt::Unchecked);
                        remember(item);
                    }
                }
            }
            emitterItem->setExpanded(true);
            for (int c = 0; c < emitterItem->childCount(); ++c)
                emitterItem->child(c)->setExpanded(true);
        }
        if (!toSelect)
        {
            toSelect = system;
            m_selection = selectionOf(system);
        }
        m_tree->setCurrentItem(toSelect);
        m_rebuildingTree = false;
    }

    void ParticleTab::select(const Selection& s)
    {
        m_selection = s;
        rebuildTree();
        rebuildDetails();
    }

    void ParticleTab::showContextMenu(const QPoint& pos)
    {
        QTreeWidgetItem* item = m_tree->itemAt(pos);
        if (!item)
            item = m_tree->currentItem();
        const Selection s = selectionOf(item);
        QMenu menu(this);

        int stage = -1;
        if (s.kind == Kind::Stage)
            stage = s.index;
        else if (s.kind == Kind::Module)
            stage = int(m_doc->data().emitters[size_t(s.emitter)].modules[size_t(s.index)].stage);
        else if (s.kind == Kind::Renderer)
            stage = kRendererStage;

        if (stage == kRendererStage)
        {
            QMenu* add = menu.addMenu(kes::style::icon("plus"), "Add Renderer");
            for (const ParticleModuleDef& def : ParticleRendererCatalog())
            {
                const QString type = QString::fromStdString(def.type);
                QAction* a = add->addAction(QString::fromStdString(def.title), this, [this, s, type] { addModule(s.emitter, kRendererStage, type); });
                a->setToolTip(QString::fromStdString(def.description));
            }
        }
        else if (stage >= 0)
        {
            QMenu* add = menu.addMenu(kes::style::icon("plus"), QString("Add Module to %1").arg(kStageTitles[stage]));
            add->setToolTipsVisible(true);
            std::map<std::string, QMenu*> categories;
            for (const ParticleModuleDef& def : ParticleModuleCatalog())
            {
                if (!def.AllowsStage(ParticleStage(stage)))
                    continue;
                QMenu*& sub = categories[def.category];
                if (!sub)
                {
                    sub = add->addMenu(QString::fromStdString(def.category));
                    sub->setToolTipsVisible(true);
                }
                const QString type = QString::fromStdString(def.type);
                QAction* a = sub->addAction(QString::fromStdString(def.title), this, [this, s, stage, type] { addModule(s.emitter, stage, type); });
                a->setToolTip(QString::fromStdString(def.description));
            }
        }

        if (s.kind == Kind::Module || s.kind == Kind::Renderer || (s.kind == Kind::Emitter && item && !item->parent()))
        {
            menu.addSeparator();
            menu.addAction(kes::style::icon("up"), "Move Up", this, [this] { moveSelected(-1); });
            menu.addAction(kes::style::icon("down"), "Move Down", this, [this] { moveSelected(1); });
            menu.addAction(kes::style::icon("copy"), "Duplicate", this, [this] { duplicateSelected(); });
            menu.addAction(kes::style::icon("trash"), "Delete", this, [this] { removeSelected(); });
        }
        menu.addSeparator();
        QMenu* emitters = menu.addMenu(kes::style::icon("plus"), "Add Emitter");
        for (const QString& name : emitterTemplateNames())
            emitters->addAction(name, this, [this, name] { addEmitter(name); });
        menu.exec(m_tree->viewport()->mapToGlobal(pos));
    }

    void ParticleTab::addModule(int emitter, int stage, const QString& type)
    {
        if (emitter < 0)
            return;
        int newIndex = 0;
        const bool renderer = stage == kRendererStage;
        const ParticleEmitterData& e = m_doc->data().emitters[size_t(emitter)];
        if (renderer)
            newIndex = int(e.renderers.size());
        else
        {
            // After the selected module of the same stage, else at the stage's end.
            const std::vector<int> list = stageModules(e, stage);
            newIndex = list.empty() ? int(std::count_if(e.modules.begin(), e.modules.end(), [stage](const ParticleModuleData& m) { return int(m.stage) < stage; }))
                                    : list.back() + 1;
            if (m_selection.kind == Kind::Module && m_selection.emitter == emitter && int(e.modules[size_t(m_selection.index)].stage) == stage)
                newIndex = m_selection.index + 1;
        }
        const std::string t = type.toStdString();
        m_doc->edit("Add " + type, [emitter, stage, renderer, newIndex, t](ParticleSystemData& d)
            {
                ParticleEmitterData& em = d.emitters[size_t(emitter)];
                if (renderer)
                    em.renderers.push_back(MakeParticleRenderer(t));
                else
                    em.modules.insert(em.modules.begin() + newIndex, MakeParticleModule(t, ParticleStage(stage)));
            },
            true);
        select({ renderer ? Kind::Renderer : Kind::Module, emitter, newIndex });
    }

    void ParticleTab::addEmitter(const QString& templateName)
    {
        ParticleEmitterData e = makeEmitterTemplate(templateName);
        const int index = int(m_doc->data().emitters.size());
        m_doc->edit("Add emitter", [e](ParticleSystemData& d) { d.emitters.push_back(e); }, true);
        select({ Kind::Emitter, index, -1 });
    }

    void ParticleTab::removeSelected()
    {
        const Selection s = currentSelection();
        if (s.kind == Kind::Module || s.kind == Kind::Renderer)
        {
            const bool renderer = s.kind == Kind::Renderer;
            m_doc->edit(renderer ? "Delete renderer" : "Delete module", [s, renderer](ParticleSystemData& d)
                {
                    auto& list = renderer ? d.emitters[size_t(s.emitter)].renderers : d.emitters[size_t(s.emitter)].modules;
                    list.erase(list.begin() + s.index);
                },
                true);
        }
        else if (s.kind == Kind::Emitter && s.index < 0)
        {
            if (m_graphEmitter == s.emitter)
                closeGraph();
            m_doc->edit("Delete emitter", [s](ParticleSystemData& d) { d.emitters.erase(d.emitters.begin() + s.emitter); }, true);
        }
    }

    void ParticleTab::moveSelected(int direction)
    {
        const Selection s = currentSelection();
        const ParticleSystemData& data = m_doc->data();
        if (s.kind == Kind::Emitter && s.index < 0)
        {
            const int target = s.emitter + direction;
            if (target < 0 || target >= int(data.emitters.size()))
                return;
            m_doc->edit("Move emitter", [s, target](ParticleSystemData& d) { std::swap(d.emitters[size_t(s.emitter)], d.emitters[size_t(target)]); }, true);
            closeGraph();
            select({ Kind::Emitter, target, -1 });
        }
        else if (s.kind == Kind::Renderer)
        {
            const int target = s.index + direction;
            if (target < 0 || target >= int(data.emitters[size_t(s.emitter)].renderers.size()))
                return;
            m_doc->edit("Move renderer", [s, target](ParticleSystemData& d)
                { std::swap(d.emitters[size_t(s.emitter)].renderers[size_t(s.index)], d.emitters[size_t(s.emitter)].renderers[size_t(target)]); },
                true);
            select({ Kind::Renderer, s.emitter, target });
        }
        else if (s.kind == Kind::Module)
        {
            const ParticleEmitterData& e = data.emitters[size_t(s.emitter)];
            const std::vector<int> list = stageModules(e, int(e.modules[size_t(s.index)].stage));
            const auto it = std::find(list.begin(), list.end(), s.index);
            const int pos = int(it - list.begin()) + direction;
            if (pos < 0 || pos >= int(list.size()))
                return;
            const int target = list[size_t(pos)];
            m_doc->edit("Move module", [s, target](ParticleSystemData& d)
                { std::swap(d.emitters[size_t(s.emitter)].modules[size_t(s.index)], d.emitters[size_t(s.emitter)].modules[size_t(target)]); },
                true);
            select({ Kind::Module, s.emitter, target });
        }
    }

    void ParticleTab::duplicateSelected()
    {
        const Selection s = currentSelection();
        if (s.kind == Kind::Module)
        {
            m_doc->edit("Duplicate module", [s](ParticleSystemData& d)
                {
                    auto& list = d.emitters[size_t(s.emitter)].modules;
                    list.insert(list.begin() + s.index + 1, list[size_t(s.index)]);
                },
                true);
            select({ Kind::Module, s.emitter, s.index + 1 });
        }
        else if (s.kind == Kind::Renderer)
        {
            m_doc->edit("Duplicate renderer", [s](ParticleSystemData& d)
                {
                    auto& list = d.emitters[size_t(s.emitter)].renderers;
                    list.insert(list.begin() + s.index + 1, list[size_t(s.index)]);
                },
                true);
            select({ Kind::Renderer, s.emitter, s.index + 1 });
        }
        else if (s.kind == Kind::Emitter && s.index < 0)
        {
            m_doc->edit("Duplicate emitter", [s](ParticleSystemData& d)
                {
                    ParticleEmitterData copy = d.emitters[size_t(s.emitter)];
                    copy.name += " Copy";
                    d.emitters.insert(d.emitters.begin() + s.emitter + 1, copy);
                },
                true);
            select({ Kind::Emitter, s.emitter + 1, -1 });
        }
    }

    // Graph panel

    void ParticleTab::openGraph(int emitter, int graphId, const QString& label)
    {
        m_graphEmitter = emitter;
        m_graphId = graphId;
        m_graphLabel->setText("Graph:  " + label);
        reloadGraph();
        if (m_graphEmitter < 0)
            return;
        m_graphStack->setCurrentIndex(1);
        // Give the graph room if the splitter was dragged nearly shut.
        const QList<int> sizes = m_centralSplit->sizes();
        if (sizes.size() == 2 && sizes[1] < 200)
        {
            const int total = sizes[0] + sizes[1];
            m_centralSplit->setSizes({ total * 55 / 100, total * 45 / 100 });
        }
        QTimer::singleShot(0, m_graphEditor->view(), [view = m_graphEditor->view()] { view->frameAll(); });
    }

    void ParticleTab::openSelectionGraph()
    {
        const Selection s = m_selection;
        if ((s.kind != Kind::Module && s.kind != Kind::Renderer) || s.emitter < 0 || s.emitter >= int(m_doc->data().emitters.size()))
            return;
        const ParticleEmitterData& e = m_doc->data().emitters[size_t(s.emitter)];
        const bool renderer = s.kind == Kind::Renderer;
        const auto& list = renderer ? e.renderers : e.modules;
        if (s.index < 0 || s.index >= int(list.size()))
            return;
        const ParticleModuleData& m = list[size_t(s.index)];
        for (const ParticleParam& p : m.params)
        {
            if (p.value.mode != ParticleValueMode::Graph || !e.FindGraph(p.value.graph))
                continue;
            if (m_graphEmitter == s.emitter && m_graphId == p.value.graph)
                return;
            openGraph(s.emitter, p.value.graph,
                QString("%1  ›  %2  ›  %3").arg(QString::fromStdString(e.name), titleOf(m, renderer), QString::fromStdString(p.name)));
            return;
        }
    }

    void ParticleTab::closeGraph()
    {
        m_graphEmitter = -1;
        m_graphId = 0;
        m_loadingGraph = true;
        m_graph->clear();
        m_graph->loadJson(graphToJson(ParticleGraphData()));
        m_loadingGraph = false;
        m_graphLabel->setText("Graph");
        m_graphStack->setCurrentIndex(0);
    }

    void ParticleTab::reloadGraph()
    {
        if (m_graphEmitter < 0)
        {
            closeGraph();
            return;
        }
        const ParticleSystemData& data = m_doc->data();
        const ParticleGraphData* graph = m_graphEmitter < int(data.emitters.size()) ? data.emitters[size_t(m_graphEmitter)].FindGraph(m_graphId) : nullptr;
        if (!graph)
        {
            closeGraph();
            return;
        }
        m_loadingGraph = true;
        QStringList warnings;
        m_graph->loadJson(graphToJson(*graph), &warnings);
        m_graph->undoStack()->clear();
        m_loadingGraph = false;
        if (!warnings.isEmpty())
            showStatus(warnings.join(' '));
    }

    // Details

    void ParticleTab::rebuildDetails()
    {
        QWidget* content = nullptr;
        const ParticleSystemData& data = m_doc->data();
        const Selection s = m_selection;
        const bool emitterValid = s.emitter >= 0 && s.emitter < int(data.emitters.size());
        switch (s.kind)
        {
        case Kind::System:
            content = systemDetails();
            break;
        case Kind::Emitter:
            if (emitterValid)
                content = emitterDetails(s.emitter);
            break;
        case Kind::Module:
            if (emitterValid && s.index < int(data.emitters[size_t(s.emitter)].modules.size()))
                content = moduleDetails(s.emitter, s.index, false);
            break;
        case Kind::Renderer:
            if (emitterValid && s.index < int(data.emitters[size_t(s.emitter)].renderers.size()))
                content = moduleDetails(s.emitter, s.index, true);
            break;
        case Kind::Stage:
            break;
        default:
            break;
        }
        if (!content)
            content = new QWidget;
        // The old panel may be running the signal that caused this rebuild.
        if (QWidget* old = m_details->takeWidget())
        {
            old->hide();
            old->deleteLater();
        }
        m_details->setWidget(content);
    }

    QWidget* ParticleTab::systemDetails()
    {
        auto* w = new QWidget;
        auto* layout = new QVBoxLayout(w);
        auto* form = new QFormLayout;
        layout->addLayout(form);
        const ParticleSystemData& data = m_doc->data();
        auto* warmup = doubleSpin(data.warmup, 0.0, 60.0, 0.1);
        warmup->setToolTip("Seconds simulated at once when the system activates (already burning fire, drifting smoke).");
        connect(warmup, &QDoubleSpinBox::valueChanged, this, [this](double v)
            { m_doc->edit("Warmup", [v](ParticleSystemData& d) { d.warmup = float(v); }, false, "warmup"); });
        form->addRow("Warmup", warmup);

        layout->addWidget(new QLabel("<b>User parameters</b>"));
        for (int i = 0; i < int(data.userParams.size()); ++i)
        {
            auto* row = new QHBoxLayout;
            auto* name = new QLineEdit(QString::fromStdString(data.userParams[size_t(i)].name));
            auto* value = doubleSpin(data.userParams[size_t(i)].value, -1e6, 1e6, 0.1);
            auto* remove = new QToolButton;
            remove->setIcon(kes::style::icon("trash"));
            row->addWidget(name, 2);
            row->addWidget(value, 1);
            row->addWidget(remove);
            layout->addLayout(row);
            connect(name, &QLineEdit::editingFinished, this, [this, i, name]
                {
                    const std::string n = name->text().trimmed().toStdString();
                    if (n.empty() || n == m_doc->data().userParams[size_t(i)].name)
                        return;
                    m_doc->edit("Rename user parameter", [i, n](ParticleSystemData& d) { d.userParams[size_t(i)].name = n; }, false);
                });
            connect(value, &QDoubleSpinBox::valueChanged, this, [this, i](double v)
                { m_doc->edit("User parameter", [i, v](ParticleSystemData& d) { d.userParams[size_t(i)].value = float(v); }, false, QString("user/%1").arg(i)); });
            connect(remove, &QToolButton::clicked, this, [this, i]
                { m_doc->edit("Remove user parameter", [i](ParticleSystemData& d) { d.userParams.erase(d.userParams.begin() + i); }, true); });
        }
        auto* add = new QPushButton(kes::style::icon("plus"), "Add User Parameter");
        connect(add, &QPushButton::clicked, this, [this]
            {
                m_doc->edit("Add user parameter", [](ParticleSystemData& d)
                    { d.userParams.push_back({ "Param" + std::to_string(d.userParams.size() + 1), 1.0f }); },
                    true);
            });
        layout->addWidget(add);
        layout->addStretch();
        return w;
    }

    QWidget* ParticleTab::emitterDetails(int emitter)
    {
        auto* w = new QWidget;
        auto* layout = new QVBoxLayout(w);
        auto* form = new QFormLayout;
        layout->addLayout(form);
        const ParticleEmitterData& e = m_doc->data().emitters[size_t(emitter)];
        auto editEmitter = [this, emitter](const QString& label, std::function<void(ParticleEmitterData&)> fn, bool structural = false)
        {
            m_doc->edit(label, [emitter, fn](ParticleSystemData& d) { fn(d.emitters[size_t(emitter)]); }, structural,
                QString("emitter/%1/%2").arg(emitter).arg(label));
        };

        auto* name = new QLineEdit(QString::fromStdString(e.name));
        connect(name, &QLineEdit::editingFinished, this, [editEmitter, name]
            { editEmitter("Name", [n = name->text().trimmed().toStdString()](ParticleEmitterData& em) { if (!n.empty()) em.name = n; }, true); });
        form->addRow("Name", name);

        auto* local = new QCheckBox;
        local->setChecked(e.localSpace);
        connect(local, &QCheckBox::toggled, this, [editEmitter](bool on) { editEmitter("Local space", [on](ParticleEmitterData& em) { em.localSpace = on; }); });
        form->addRow("Local space", local);

        auto* maxParticles = intSpin(int(e.maxParticles), 1, 1000000);
        connect(maxParticles, &QSpinBox::valueChanged, this, [editEmitter](int v)
            { editEmitter("Max particles", [v](ParticleEmitterData& em) { em.maxParticles = uint32_t(v); }); });
        form->addRow("Max particles", maxParticles);

        auto* duration = doubleSpin(e.duration, 0.0, 3600.0, 0.1);
        duration->setToolTip("Seconds per loop; 0 = endless");
        connect(duration, &QDoubleSpinBox::valueChanged, this, [editEmitter](double v)
            { editEmitter("Duration", [v](ParticleEmitterData& em) { em.duration = float(v); }); });
        form->addRow("Duration", duration);

        auto* loop = new QCheckBox;
        loop->setChecked(e.loop);
        connect(loop, &QCheckBox::toggled, this, [editEmitter](bool on) { editEmitter("Loop", [on](ParticleEmitterData& em) { em.loop = on; }); });
        form->addRow("Loop", loop);

        auto* loopCount = intSpin(int(e.loopCount), 0, 100000);
        loopCount->setToolTip("0 = forever");
        connect(loopCount, &QSpinBox::valueChanged, this, [editEmitter](int v)
            { editEmitter("Loop count", [v](ParticleEmitterData& em) { em.loopCount = uint32_t(v); }); });
        form->addRow("Loop count", loopCount);

        auto* delay = doubleSpin(e.delay, 0.0, 3600.0, 0.1);
        connect(delay, &QDoubleSpinBox::valueChanged, this, [editEmitter](double v)
            { editEmitter("Delay", [v](ParticleEmitterData& em) { em.delay = float(v); }); });
        form->addRow("Delay", delay);

        auto* seed = intSpin(int(e.seed), 0, 1000000);
        seed->setToolTip("0 = a different random sequence every activation");
        connect(seed, &QSpinBox::valueChanged, this, [editEmitter](int v)
            { editEmitter("Seed", [v](ParticleEmitterData& em) { em.seed = uint32_t(v); }); });
        form->addRow("Seed", seed);

        QStringList custom;
        for (const std::string& a : e.customAttributes)
            custom << QString::fromStdString(a);
        auto* attributes = new QLineEdit(custom.join(", "));
        attributes->setToolTip("Comma separated float4 attributes for Set Attribute and the Custom Attribute graph node");
        connect(attributes, &QLineEdit::editingFinished, this, [editEmitter, attributes]
            {
                std::vector<std::string> list;
                for (const QString& part : attributes->text().split(',', Qt::SkipEmptyParts))
                    if (!part.trimmed().isEmpty())
                        list.push_back(part.trimmed().toStdString());
                editEmitter("Custom attributes", [list](ParticleEmitterData& em) { em.customAttributes = list; });
            });
        form->addRow("Custom attributes", attributes);
        layout->addStretch();
        return w;
    }

    QWidget* ParticleTab::moduleDetails(int emitter, int index, bool renderer)
    {
        auto* w = new QWidget;
        auto* layout = new QVBoxLayout(w);
        const ParticleEmitterData& e = m_doc->data().emitters[size_t(emitter)];
        const ParticleModuleData& m = renderer ? e.renderers[size_t(index)] : e.modules[size_t(index)];
        const ParticleModuleDef* def = defOf(m, renderer);
        auto* title = new QLabel(QString("<b>%1</b>").arg(titleOf(m, renderer)));
        layout->addWidget(title);
        if (!def)
        {
            layout->addWidget(note("Unknown module type."));
            layout->addStretch();
            return w;
        }

        auto* form = new QFormLayout;
        form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
        layout->addLayout(form);

        auto editParam = [this, emitter, index, renderer](const std::string& name, std::function<void(ParticleParam&)> fn, bool structural = false)
        {
            m_doc->edit(QString("Edit %1").arg(QString::fromStdString(name)), [emitter, index, renderer, name, fn](ParticleSystemData& d)
                {
                    ParticleEmitterData& em = d.emitters[size_t(emitter)];
                    ParticleModuleData& mod = renderer ? em.renderers[size_t(index)] : em.modules[size_t(index)];
                    if (ParticleParam* p = mod.Find(name))
                        fn(*p);
                },
                structural, QString("param/%1/%2/%3/%4").arg(emitter).arg(renderer).arg(index).arg(QString::fromStdString(name)));
        };

        for (const ParticleParamDef& pd : def->params)
        {
            const ParticleParam* param = m.Find(pd.name);
            if (!param)
                continue;
            const std::string name = pd.name;
            QString label = QString::fromStdString(pd.name);
            if (pd.degrees)
                label += QString::fromUtf8(" (°)");
            QWidget* editor = nullptr;
            switch (pd.kind)
            {
            case ParticleParamKind::Value:
            {
                auto* ve = new ValueEditor(pd, param->value);
                connect(ve, &ValueEditor::valueChanged, this, [editParam, name](const ParticleValue& v)
                    { editParam(name, [v](ParticleParam& p) { p.value = v; }); });
                connect(ve, &ValueEditor::graphRequested, this, [this, emitter, index, renderer, name, pd, label]
                    {
                        const ParticleEmitterData& em = m_doc->data().emitters[size_t(emitter)];
                        const ParticleModuleData& mod = renderer ? em.renderers[size_t(index)] : em.modules[size_t(index)];
                        const ParticleParam* p = mod.Find(name);
                        if (!p)
                            return;
                        const int id = em.NextGraphId();
                        const ParticleGraphData graph = makeStarterGraph(id, pd.dim, p->value.a);
                        m_doc->edit("Use graph", [emitter, index, renderer, name, graph](ParticleSystemData& d)
                            {
                                ParticleEmitterData& target = d.emitters[size_t(emitter)];
                                target.graphs.push_back(graph);
                                ParticleModuleData& mod = renderer ? target.renderers[size_t(index)] : target.modules[size_t(index)];
                                if (ParticleParam* pp = mod.Find(name))
                                {
                                    pp->value.mode = ParticleValueMode::Graph;
                                    pp->value.graph = graph.id;
                                }
                            },
                            true);
                        openGraph(emitter, id, QString("%1  ›  %2  ›  %3").arg(QString::fromStdString(em.name), titleOf(mod, renderer), label));
                    });
                connect(ve, &ValueEditor::editGraphRequested, this, [this, emitter, index, renderer, label](int id)
                    {
                        const ParticleEmitterData& em = m_doc->data().emitters[size_t(emitter)];
                        const ParticleModuleData& mod = renderer ? em.renderers[size_t(index)] : em.modules[size_t(index)];
                        if (!em.FindGraph(id))
                        {
                            showStatus(QString("Graph %1 is missing; pick Graph again to make a new one.").arg(id));
                            return;
                        }
                        openGraph(emitter, id, QString("%1  ›  %2  ›  %3").arg(QString::fromStdString(em.name), titleOf(mod, renderer), label));
                    });
                editor = ve;
                break;
            }
            case ParticleParamKind::Enum:
            {
                auto* combo = new QComboBox;
                combo->addItems(QString::fromStdString(pd.options).split('|'));
                combo->setCurrentText(QString::fromStdString(param->text));
                connect(combo, &QComboBox::currentTextChanged, this, [editParam, name](const QString& t)
                    { editParam(name, [v = t.toStdString()](ParticleParam& p) { p.text = v; }); });
                editor = combo;
                break;
            }
            case ParticleParamKind::Bool:
            {
                auto* check = new QCheckBox;
                check->setChecked(param->text == "true" || param->text == "1");
                connect(check, &QCheckBox::toggled, this, [editParam, name](bool on)
                    { editParam(name, [on](ParticleParam& p) { p.text = on ? "true" : "false"; }); });
                editor = check;
                break;
            }
            case ParticleParamKind::Int:
            {
                auto* spin = intSpin(QString::fromStdString(param->text).toInt(), 0, 1000000);
                connect(spin, &QSpinBox::valueChanged, this, [editParam, name](int v)
                    { editParam(name, [v](ParticleParam& p) { p.text = std::to_string(v); }); });
                editor = spin;
                break;
            }
            case ParticleParamKind::Asset:
            {
                auto* row = new QWidget;
                auto* h = new QHBoxLayout(row);
                h->setContentsMargins(0, 0, 0, 0);
                auto* edit = new QLineEdit(QString::fromStdString(param->text));
                edit->setPlaceholderText("content path");
                auto* browse = new QToolButton;
                browse->setIcon(kes::style::icon("open"));
                h->addWidget(edit, 1);
                h->addWidget(browse);
                connect(edit, &QLineEdit::editingFinished, this, [editParam, name, edit]
                    { editParam(name, [v = edit->text().trimmed().toStdString()](ParticleParam& p) { p.text = v; }); });
                const QString extensions = QString::fromStdString(pd.options);
                connect(browse, &QToolButton::clicked, this, [this, editParam, name, edit, extensions]
                    {
                        QStringList patterns;
                        for (const QString& ext : extensions.split('|', Qt::SkipEmptyParts))
                            patterns << "*." + ext;
                        const QString path = QFileDialog::getOpenFileName(this, "Choose Asset", m_contentRoot,
                            QString("Assets (%1)").arg(patterns.join(' ')));
                        if (path.isEmpty())
                            return;
                        QString relative = forge::contentRelativePath(m_contentRoot, path);
                        if (relative.isEmpty())
                        {
                            showStatus("The file is outside the content folder; the game won't find it.");
                            relative = path;
                        }
                        edit->setText(relative);
                        editParam(name, [v = relative.toStdString()](ParticleParam& p) { p.text = v; });
                    });
                editor = row;
                break;
            }
            case ParticleParamKind::Text:
            {
                auto* edit = new QLineEdit(QString::fromStdString(param->text));
                connect(edit, &QLineEdit::editingFinished, this, [editParam, name, edit]
                    { editParam(name, [v = edit->text().trimmed().toStdString()](ParticleParam& p) { p.text = v; }); });
                editor = edit;
                break;
            }
            }
            if (!editor)
                continue;
            auto* labelWidget = new QLabel(label);
            labelWidget->setToolTip(QString::fromStdString(pd.tooltip));
            editor->setToolTip(QString::fromStdString(pd.tooltip));
            form->addRow(labelWidget, editor);
        }
        layout->addStretch();
        return w;
    }
}
