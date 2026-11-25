#pragma once

#include "SlangCodeGen.h"
#include "framework/EditorModule.h"
#include "framework/EditorTab.h"

#include <QTimer>
#include <array>

class QComboBox;
class QLabel;
class QListWidget;
class QPlainTextEdit;
class QTabWidget;

namespace nodegraph
{
    class GraphEditor;
}

namespace sne
{

    class GraphModel;
    class MaterialPanel;
    class PreviewCompiler;
    class PreviewWidget;
    class SlangHighlighter;

    // Node graph that generates a Slang surface shader and exports .mat files, with a live preview.
    class ShaderGraphTab : public EditorTab
    {
        Q_OBJECT
    public:
        explicit ShaderGraphTab(EditorContext* ctx, QWidget* parent = nullptr);

        QString filePath() const override { return m_currentFile; }
        QString displayName() const override;
        bool isModified() const override;
        bool openFile(const QString& path, QString* error) override;
        bool save() override;
        bool saveAs() override;
        bool handlesFile(const QString& path) const override;
        void projectChanged() override;

    private:
        void buildDocks();
        void buildActions();
        QWidget* buildPreviewPanel();
        void loadExample();
        bool saveTo(const QString& path);
        bool openMaterial(const QString& matPath, QString* error);
        void exportSlang();
        void copySlang();
        void exportMaterial();
        void locateSlangc();
        void detectSlangc();
        void regenerate();
        void refreshProblems();
        void updateStatus();
        QString graphName() const;
        void setContentRoot(const QString& root);
        void setPreviewErrors(const QString& log);
        void syncMaterial();

        GraphModel* m_model;
        nodegraph::GraphEditor* m_editor;
        MaterialPanel* m_materialPanel;
        PreviewWidget* m_preview;
        PreviewCompiler* m_compiler;

        QPlainTextEdit* m_code = nullptr;
        QListWidget* m_problems = nullptr;
        QTabWidget* m_bottomTabs = nullptr;
        SlangHighlighter* m_highlighter = nullptr;
        QLabel* m_infoLabel = nullptr;
        QLabel* m_zoomLabel = nullptr;
        QLabel* m_genLabel = nullptr;
        QLabel* m_previewStatus = nullptr;
        QComboBox* m_targetCombo = nullptr;
        QList<QAction*> m_dockToggles;

        QTimer m_genTimer;
        CodeGenResult m_gen;
        ShaderTarget m_target = ShaderTarget::KesEngine;
        QString m_currentFile;
        // The .mat this graph was opened from or last exported to; Export Material defaults to it.
        QString m_materialPath;

        QString m_contentRoot;
        QString m_previewSource;
        QStringList m_previewErrors;
        std::array<QString, MaterialSlotCount> m_textureProblems;
        std::array<QString, MaterialSlotCount> m_loadedTextures;
    };

    class ShaderGraphModule : public EditorModule
    {
    public:
        QString id() const override { return QStringLiteral("shaderGraph"); }
        QString name() const override { return QStringLiteral("Shader Graph"); }
        QStringList extensions() const override { return { QStringLiteral("slangnodes"), QStringLiteral("mat") }; }
        QString newLabel() const override { return QStringLiteral("Shader Graph"); }
        EditorTab* createTab(EditorContext* ctx) const override { return new ShaderGraphTab(ctx); }
    };

} // namespace sne
