#include "ShaderGraphTab.h"

#include "ExampleGraphs.h"
#include "GraphModel.h"
#include "KesProject.h"
#include "MaterialExport.h"
#include "core/MaterialLibrary.h"
#include "MaterialPanel.h"
#include "PreviewCompiler.h"
#include "PreviewWidget.h"
#include "SlangHighlighter.h"
#include "core/EditorContext.h"
#include "framework/graph/GraphEditor.h"
#include "framework/graph/InspectorWidget.h"
#include "framework/graph/NodeGraphView.h"
#include "ui/KesStyle.h"

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QCoreApplication>
#include <QDataStream>
#include <QDir>
#include <QDockWidget>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QImage>
#include <QJsonDocument>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QScrollArea>
#include <QScrollBar>
#include <QSettings>
#include <QStatusBar>
#include <QTabWidget>
#include <QToolBar>
#include <QUndoStack>
#include <QVBoxLayout>

namespace sne
{

    namespace
    {

        QPlainTextEdit* makeCodeView(bool readOnly)
        {
            auto* e = new QPlainTextEdit;
            e->setReadOnly(readOnly);
            e->setLineWrapMode(QPlainTextEdit::NoWrap);
            QFont f("monospace");
            f.setStyleHint(QFont::Monospace);
            f.setPointSizeF(9.5);
            e->setFont(f);
            e->setTabStopDistance(QFontMetricsF(f).horizontalAdvance(' ') * 4);
            return e;
        }

        QImage loadTextureImage(const QString& path)
        {
            if (QFileInfo(path).suffix().compare("ktex", Qt::CaseInsensitive) == 0) return MaterialLibrary::loadTexture(path);
            return QImage(path);
        }

    } // namespace

    ShaderGraphTab::ShaderGraphTab(EditorContext* ctx, QWidget* parent)
        : EditorTab(ctx, QStringLiteral("shaderGraph"), parent)
    {
        m_model = new GraphModel(this);
        m_editor = new nodegraph::GraphEditor(m_model, this);
        m_materialPanel = new MaterialPanel(m_model, this);
        m_preview = new PreviewWidget(this);
        m_compiler = new PreviewCompiler(this);

        setCentralWidget(m_editor->view());
        buildDocks();
        buildActions();

        m_infoLabel = new QLabel;
        m_zoomLabel = new QLabel("100%");
        m_genLabel = new QLabel;
        statusBar()->addPermanentWidget(m_genLabel);
        statusBar()->addPermanentWidget(m_infoLabel);
        statusBar()->addPermanentWidget(m_zoomLabel);

        m_genTimer.setSingleShot(true);
        m_genTimer.setInterval(120);
        connect(&m_genTimer, &QTimer::timeout, this, &ShaderGraphTab::regenerate);

        connect(m_model, &GraphModel::changed, this, [this]
            {
        m_genTimer.start();
        updateStatus(); });
        connect(m_model, &GraphModel::materialChanged, this, &ShaderGraphTab::syncMaterial);
        connect(m_model->undoStack(), &QUndoStack::cleanChanged, this, &EditorTab::titleChanged);

        connect(m_compiler, &PreviewCompiler::compiled, this, [this](const QString& vertex, const QString& fragment)
            {
        setPreviewErrors(QString());
        m_preview->setProgram(vertex, fragment); });
        connect(m_compiler, &PreviewCompiler::failed, this, &ShaderGraphTab::setPreviewErrors);
        connect(m_preview, &PreviewWidget::shaderRejected, this, &ShaderGraphTab::setPreviewErrors);
        connect(m_preview, &PreviewWidget::rendererFailed, this, &ShaderGraphTab::setPreviewErrors);
        connect(m_materialPanel, &MaterialPanel::contentRootFound, this, &ShaderGraphTab::setContentRoot);
        connect(m_editor, &nodegraph::GraphEditor::statusMessage, this, [this](const QString& s)
            { showStatus(s); });
        connect(m_editor->view(), &nodegraph::NodeGraphView::zoomChanged, this,
            [this](double z)
            { m_zoomLabel->setText(QString("%1%").arg(qRound(z * 100))); });

        buildEmptyGraph(*m_model);
        m_model->undoStack()->clear();
        m_model->undoStack()->setClean();

        setContentRoot(ctx->projectDir());
        detectSlangc();
        syncMaterial();
        regenerate();
        updateStatus();
        restoreLayout();
        QTimer::singleShot(0, this, [this]
            { m_editor->view()->frameAll(); });
    }

    void ShaderGraphTab::buildDocks()
    {
        auto* libDock = new QDockWidget("Node Library", this);
        libDock->setObjectName("LibraryDock");
        libDock->setWidget(m_editor->library());
        addDockWidget(Qt::LeftDockWidgetArea, libDock);

        auto* inspDock = new QDockWidget("Inspector", this);
        inspDock->setObjectName("InspectorDock");
        inspDock->setWidget(m_editor->inspector());
        addDockWidget(Qt::RightDockWidgetArea, inspDock);

        m_code = makeCodeView(true);
        m_highlighter = new SlangHighlighter(m_code->document());

        auto* bottomDock = new QDockWidget("Slang Source", this);
        bottomDock->setObjectName("ShaderDock");
        bottomDock->setWidget(m_code);
        addDockWidget(Qt::BottomDockWidgetArea, bottomDock);
        auto* previewDock = new QDockWidget("Preview", this);
        previewDock->setObjectName("PreviewDock");
        previewDock->setWidget(buildPreviewPanel());
        addDockWidget(Qt::RightDockWidgetArea, previewDock);
        splitDockWidget(previewDock, inspDock, Qt::Vertical);

        auto* materialScroll = new QScrollArea;
        materialScroll->setWidget(m_materialPanel);
        materialScroll->setWidgetResizable(true);
        materialScroll->setFrameShape(QFrame::NoFrame);
        auto* materialDock = new QDockWidget("Material", this);
        materialDock->setObjectName("MaterialDock");
        materialDock->setWidget(materialScroll);
        addDockWidget(Qt::RightDockWidgetArea, materialDock);
        tabifyDockWidget(inspDock, materialDock);
        inspDock->raise();

        resizeDocks({ bottomDock }, { 220 }, Qt::Vertical);
        resizeDocks({ libDock, inspDock }, { 250, 320 }, Qt::Horizontal);
        resizeDocks({ previewDock }, { 320 }, Qt::Vertical);

        m_dockToggles = { libDock->toggleViewAction(), inspDock->toggleViewAction(), materialDock->toggleViewAction(),
            previewDock->toggleViewAction(), bottomDock->toggleViewAction() };
    }

    QWidget* ShaderGraphTab::buildPreviewPanel()
    {
        auto* panel = new QWidget;
        auto* layout = new QVBoxLayout(panel);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(4);
        layout->addWidget(m_preview, 1);

        auto* shape = new QComboBox;
        shape->addItems({ "Sphere", "Cube", "Plane", "Torus" });
        connect(shape, &QComboBox::currentIndexChanged, this, [this](int index)
            { m_preview->setShape(PreviewShape(index)); });
        m_previewStatus = new QLabel;

        auto* row = new QHBoxLayout;
        row->setContentsMargins(6, 0, 6, 4);
        row->addWidget(shape);
        row->addWidget(m_previewStatus, 1);
        layout->addLayout(row);
        return panel;
    }

    void ShaderGraphTab::buildActions()
    {
        auto make = [this](const QString& text, const QKeySequence& ks, auto slot, const QString& icon = {})
        {
            auto* a = new QAction(text, this);
            if (!ks.isEmpty()) a->setShortcut(ks);
            if (!icon.isEmpty()) a->setIcon(kes::style::icon(icon));
            connect(a, &QAction::triggered, this, slot);
            return a;
        };

        QAction* undo = m_model->undoStack()->createUndoAction(this, "&Undo");
        undo->setShortcut(QKeySequence::Undo);
        undo->setIcon(kes::style::icon("undo"));
        QAction* redo = m_model->undoStack()->createRedoAction(this, "&Redo");
        redo->setShortcuts({ QKeySequence::Redo, QKeySequence("Ctrl+Y") });
        redo->setIcon(kes::style::icon("redo"));

        const nodegraph::GraphEditor::Actions g = m_editor->createActions(this);
        // window shortcuts would fire from the docks too; F and Delete only make sense on the canvas
        for (QAction* a : { g.frameAll, g.remove })
        {
            a->setShortcutContext(Qt::WidgetWithChildrenShortcut);
            m_editor->view()->addAction(a);
        }

        QAction* exportSlangAct = make(QString::fromUtf8("&Export Slang…"), QKeySequence("Ctrl+E"), [this]
            { exportSlang(); });
        QAction* copySlangAct = make("Copy Slang to &Clipboard", QKeySequence("Ctrl+Shift+C"), [this]
            { copySlang(); });
        QAction* exportMaterialAct = make(QString::fromUtf8("Export &Material…"), QKeySequence("Ctrl+Shift+E"), [this]
            { exportMaterial(); }, "save");
        QAction* exampleAct = make("Load E&xample Graph", {}, [this]
            { loadExample(); });
        QAction* slangcAct = make(QString::fromUtf8("&Locate slangc…"), {}, [this]
            { locateSlangc(); });

        QMenu* edit = addTabMenu("&Edit");
        edit->addActions({ undo, redo });
        edit->addSeparator();
        edit->addActions({ g.cut, g.copy, g.paste, g.duplicate, g.remove });
        edit->addSeparator();
        edit->addActions({ g.selectAll, g.addNode });

        QMenu* view = addTabMenu("&View");
        view->addActions({ g.frameAll, g.frameSelection, g.zoomIn, g.zoomOut, g.zoomReset });
        view->addSeparator();
        view->addAction(g.snap);
        view->addSeparator();
        view->addActions(m_dockToggles);

        QMenu* shader = addTabMenu("&Shader");
        shader->addAction(exportMaterialAct);
        shader->addAction(exportSlangAct);
        shader->addAction(copySlangAct);
        shader->addSeparator();
        shader->addAction(exampleAct);
        shader->addAction(slangcAct);

        auto* tb = addToolBar("Shader Graph");
        tb->setObjectName("ShaderGraphToolBar");
        tb->setMovable(false);
        tb->setIconSize(QSize(18, 18));
        tb->addAction(undo);
        tb->addAction(redo);
        tb->addSeparator();
        tb->addAction(g.addNode);
        tb->addAction(g.frameAll);
        tb->addSeparator();
        tb->addAction(exportSlangAct);
        tb->addAction(exportMaterialAct);
        tb->addSeparator();

        m_targetCombo = new QComboBox;
        m_targetCombo->addItems({ "KES Engine", "Standalone Slang" });
        m_targetCombo->setToolTip("Target for the Slang Source tab and Export Slang. The preview always uses the KES target.");
        connect(m_targetCombo, &QComboBox::currentIndexChanged, this, [this](int index)
            {
        m_target = index == 0 ? ShaderTarget::KesEngine : ShaderTarget::Standalone;
        regenerate(); });
        tb->addWidget(m_targetCombo);
    }

    QString ShaderGraphTab::graphName() const
    {
        if (!m_currentFile.isEmpty()) return QFileInfo(m_currentFile).completeBaseName();
        if (!m_materialPath.isEmpty()) return QFileInfo(m_materialPath).completeBaseName();
        return QStringLiteral("Untitled");
    }

    QString ShaderGraphTab::displayName() const
    {
        if (!m_currentFile.isEmpty()) return QFileInfo(m_currentFile).fileName();
        if (!m_materialPath.isEmpty()) return QFileInfo(m_materialPath).fileName();
        return QStringLiteral("Untitled Shader Graph");
    }

    bool ShaderGraphTab::isModified() const
    {
        return !m_model->undoStack()->isClean();
    }

    bool ShaderGraphTab::handlesFile(const QString& path) const
    {
        if (EditorTab::handlesFile(path)) return true;
        return !m_materialPath.isEmpty() &&
            QFileInfo(m_materialPath).absoluteFilePath().compare(QFileInfo(path).absoluteFilePath(), Qt::CaseInsensitive) == 0;
    }

    void ShaderGraphTab::projectChanged()
    {
        setContentRoot(context()->projectDir());
        detectSlangc();
        m_previewSource.clear();
        regenerate();
    }

    void ShaderGraphTab::updateStatus()
    {
        m_infoLabel->setText(QString::fromUtf8("  %1 nodes · %2 connections  ").arg(m_model->nodeCount()).arg(m_model->connections().size()));
    }

    void ShaderGraphTab::loadExample()
    {
        if (!canClose()) return;
        buildExampleGraph(*m_model);
        m_model->undoStack()->clear();
        m_model->undoStack()->setClean();
        m_currentFile.clear();
        m_materialPath.clear();
        emit titleChanged();
        m_editor->view()->frameAll();
    }

    bool ShaderGraphTab::openFile(const QString& path, QString* error)
    {
        if (QFileInfo(path).suffix().compare("mat", Qt::CaseInsensitive) == 0) return openMaterial(path, error);

        QFile f(path);
        if (!f.open(QIODevice::ReadOnly))
        {
            if (error) *error = QString("Could not read %1:\n%2").arg(path, f.errorString());
            return false;
        }
        QJsonParseError err;
        const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
        if (!doc.isObject())
        {
            if (error) *error = QString("%1 is not a valid graph file.\n%2").arg(path, err.errorString());
            return false;
        }
        QStringList warnings;
        if (!m_model->loadJson(doc.object(), &warnings))
        {
            if (error) *error = warnings.join('\n');
            return false;
        }
        m_model->undoStack()->clear();
        m_model->undoStack()->setClean();
        m_currentFile = QFileInfo(path).absoluteFilePath();
        m_materialPath.clear();
        if (m_contentRoot.isEmpty()) setContentRoot(findContentRoot(path));
        emit titleChanged();
        QTimer::singleShot(0, this, [this]
            { m_editor->view()->frameAll(); });
        if (!warnings.isEmpty()) QMessageBox::information(this, "Loaded with warnings", warnings.join('\n'));
        return true;
    }

    // A .mat opens its graph (name.slangnodes next to it) when there is one; otherwise a new
    // graph starts from the material's values, and Export Material writes back to the same file.
    bool ShaderGraphTab::openMaterial(const QString& matPath, QString* error)
    {
        const QFileInfo info(matPath);
        const QString graphPath = info.dir().absoluteFilePath(info.completeBaseName() + ".slangnodes");
        if (QFileInfo::exists(graphPath))
        {
            if (!openFile(graphPath, error)) return false;
            m_materialPath = info.absoluteFilePath();
            return true;
        }

        QFile f(matPath);
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        {
            if (error) *error = QString("Could not read %1:\n%2").arg(matPath, f.errorString());
            return false;
        }
        const MaterialSettings material = MaterialSettings::fromMatFile(QString::fromUtf8(f.readAll()));

        if (m_contentRoot.isEmpty()) setContentRoot(findContentRoot(matPath));
        buildEmptyGraph(*m_model);
        m_model->setMaterial(material);
        m_model->undoStack()->clear();
        m_model->undoStack()->setClean();
        m_currentFile.clear();
        m_materialPath = info.absoluteFilePath();
        emit titleChanged();
        QTimer::singleShot(0, this, [this]
            { m_editor->view()->frameAll(); });
        showStatus("Opened material " + matPath + ". Save the graph next to it to keep the nodes.", 6000);
        return true;
    }

    bool ShaderGraphTab::saveTo(const QString& path)
    {
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        {
            QMessageBox::warning(this, "Save failed", QString("Could not write %1:\n%2").arg(path, f.errorString()));
            return false;
        }
        f.write(QJsonDocument(m_model->toJson()).toJson(QJsonDocument::Indented));
        m_currentFile = QFileInfo(path).absoluteFilePath();
        m_model->undoStack()->setClean();
        emit titleChanged();
        emit fileSaved(m_currentFile);
        showStatus("Saved " + path, 3000);
        return true;
    }

    bool ShaderGraphTab::save()
    {
        return m_currentFile.isEmpty() ? saveAs() : saveTo(m_currentFile);
    }

    bool ShaderGraphTab::saveAs()
    {
        const QFileInfo material(m_materialPath);
        QString suggested;
        if (!m_currentFile.isEmpty())
            suggested = m_currentFile;
        else if (!m_materialPath.isEmpty())
            suggested = material.dir().absoluteFilePath(material.completeBaseName() + ".slangnodes");
        else
            suggested = QDir(m_contentRoot.isEmpty() ? QString() : QDir(m_contentRoot).absoluteFilePath("materials")).absoluteFilePath(graphName() + ".slangnodes");
        QString path = QFileDialog::getSaveFileName(this, "Save graph", suggested, "Slang node graphs (*.slangnodes);;JSON (*.json)");
        if (path.isEmpty()) return false;
        if (QFileInfo(path).suffix().isEmpty()) path += ".slangnodes";
        return saveTo(path);
    }

    void ShaderGraphTab::exportSlang()
    {
        regenerate();
        if (!m_gen.ok)
        {
            QMessageBox::warning(this, "Cannot export", m_gen.errors.join('\n'));
            return;
        }
        const QString path = QFileDialog::getSaveFileName(this, "Export Slang shader", graphName() + ".slang", "Slang shaders (*.slang)");
        if (path.isEmpty()) return;
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        {
            QMessageBox::warning(this, "Export failed", f.errorString());
            return;
        }
        f.write(m_gen.code.toUtf8());
        showStatus("Exported " + path, 3000);
    }

    void ShaderGraphTab::exportMaterial()
    {
        regenerate();

        const QString folder = m_contentRoot.isEmpty() ? QString() : QDir(m_contentRoot).absoluteFilePath("materials");
        const QString suggested = !m_materialPath.isEmpty() ? m_materialPath : QDir(folder).absoluteFilePath(graphName() + ".mat");
        QString path = QFileDialog::getSaveFileName(this, "Export material", suggested, "KES materials (*.mat)");
        if (path.isEmpty()) return;
        if (QFileInfo(path).suffix().isEmpty()) path += ".mat";

        if (m_contentRoot.isEmpty()) setContentRoot(findContentRoot(path));

        MaterialFiles files;
        QString error;
        if (!buildMaterialFiles(*m_model, path, m_contentRoot, files, &error))
        {
            QMessageBox::warning(this, "Cannot export", error);
            return;
        }

        if (files.hasShader() && !m_previewErrors.isEmpty())
        {
            const auto r = QMessageBox::question(this, "Shader has errors",
                "The preview could not compile this shader, so the engine build will fail on it too.\n"
                "Export anyway?");
            if (r != QMessageBox::Yes) return;
        }
        const QStringList handWritten = handWrittenFiles(files);
        if (!handWritten.isEmpty())
        {
            const auto r = QMessageBox::question(this, "Overwrite files",
                QString("These files weren't written by this tool:\n\n%1\n\nOverwrite them?").arg(handWritten.join('\n')));
            if (r != QMessageBox::Yes) return;
        }

        if (!writeMaterialFiles(files, &error))
        {
            QMessageBox::warning(this, "Export failed", error);
            return;
        }
        m_materialPath = QFileInfo(path).absoluteFilePath();
        emit titleChanged();
        emit fileSaved(m_materialPath);
        showStatus(files.hasShader() ? "Exported " + path + ". Rebuild the game to compile its shader." : "Exported " + path, 6000);
    }

    void ShaderGraphTab::locateSlangc()
    {
        const QString path = QFileDialog::getOpenFileName(this, "Locate slangc", QFileInfo(m_compiler->slangc()).absolutePath());
        if (path.isEmpty()) return;
        QSettings().setValue("shaderGraph/slangcPath", path);
        detectSlangc();
        m_previewSource.clear();
        regenerate();
    }

    void ShaderGraphTab::detectSlangc()
    {
        QString path = QSettings().value("shaderGraph/slangcPath").toString();
        if (!QFileInfo::exists(path))
        {
            const QString projectDir = context()->project().isValid() ? QFileInfo(context()->project().filePath).absolutePath() : QString();
            path = findSlangc({ projectDir, context()->projectDir(), QCoreApplication::applicationDirPath(), QDir::currentPath() });
        }
        m_compiler->setSlangc(path);
    }

    void ShaderGraphTab::setContentRoot(const QString& root)
    {
        m_contentRoot = root;
        m_materialPanel->setContentRoot(root);
        m_loadedTextures.fill(QString());
        syncMaterial();
    }

    void ShaderGraphTab::syncMaterial()
    {
        const MaterialSettings& material = m_model->material();
        m_preview->setMaterial(material);

        for (int i = 0; i < MaterialSlotCount; ++i)
        {
            const QString& relative = material.textures[i];
            if (relative == m_loadedTextures[i]) continue;
            m_loadedTextures[i] = relative;

            QImage image;
            if (!relative.isEmpty()) image = loadTextureImage(QDir(m_contentRoot).absoluteFilePath(relative));
            m_textureProblems[i] = !relative.isEmpty() && image.isNull() ? QString("Preview can't load texture %1.").arg(relative) : QString();
            m_preview->setTexture(i, image);
        }
        refreshProblems();
    }

    void ShaderGraphTab::setPreviewErrors(const QString& log)
    {
        m_previewErrors = log.split('\n', Qt::SkipEmptyParts);
        m_previewStatus->setText(m_previewErrors.isEmpty() ? QString() : QString::fromUtf8("⚠ see Problems"));
        refreshProblems();
    }

    void ShaderGraphTab::copySlang()
    {
        regenerate();
        QApplication::clipboard()->setText(m_gen.code);
        showStatus("Slang source copied to clipboard", 2500);
    }

    void ShaderGraphTab::regenerate()
    {
        m_genTimer.stop();
        m_gen = SlangCodeGen::generate(*m_model, graphName(), m_target);

        const int scroll = m_code->verticalScrollBar()->value();
        m_code->setPlainText(m_gen.ok ? m_gen.code : QString("// Cannot generate shader:\n// %1\n").arg(m_gen.errors.join("\n// ")));
        m_code->verticalScrollBar()->setValue(scroll);

        if (m_genLabel) m_genLabel->setText(m_gen.ok ? QString() : QString::fromUtf8("⚠ shader has errors"));
        refreshProblems();

        const CodeGenResult engine = m_target == ShaderTarget::KesEngine ? m_gen : SlangCodeGen::generate(*m_model, graphName(), ShaderTarget::KesEngine);
        if (engine.ok && engine.code != m_previewSource)
        {
            m_previewSource = engine.code;
            m_preview->setAnimated(engine.code.contains("KesTime()"));
            m_compiler->compile(engine.code);
        }
    }

    void ShaderGraphTab::refreshProblems()
    {
        ProblemList problems;
        auto add = [&](bool error, const QString& text)
        {
            Problem p;
            p.severity = error ? ProblemSeverity::Error : ProblemSeverity::Warning;
            p.message = text;
            problems << p;
        };
        for (const QString& e : m_gen.errors)
            add(true, e);
        for (const QString& w : m_gen.warnings)
            add(false, w);
        for (const QString& e : m_previewErrors)
            add(true, e);
        for (const QString& w : m_textureProblems)
            if (!w.isEmpty()) add(false, w);

        setProblems(problems);
    }

} // namespace sne
