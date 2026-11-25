#pragma once

#include "framework/graph/GraphTypes.h"

#include <QList>
#include <QWidget>
#include <functional>

class QVBoxLayout;

namespace nodegraph
{
    class GraphModel;

    // Edits the parameters and unconnected inputs of the selected node.
    class InspectorWidget : public QWidget
    {
        Q_OBJECT
    public:
        // Returns an editor for params the generic inspector can't edit (e.g. clip pickers),
        // or nullptr to use the default one. apply() records an undoable change.
        using ParamEditorFactory = std::function<QWidget*(int nodeId, const ParamDef& param, const QVariant& current,
            const std::function<void(const QVariant&)>& apply)>;

        explicit InspectorWidget(GraphModel* model, QWidget* parent = nullptr);

        void setSelection(const QList<int>& ids);
        void setParamEditorFactory(ParamEditorFactory factory);

    public slots:
        void scheduleRebuild();

    private slots:
        void rebuild();

    private:
        QWidget* makeParamEditor(int nodeId, int paramIndex);
        QWidget* makeDefaultEditor(int nodeId, int inputIndex);

        GraphModel* m_model;
        ParamEditorFactory m_factory;
        QVBoxLayout* m_root;
        QWidget* m_content = nullptr;
        QList<int> m_selection;
        bool m_internalEdit = false;
        bool m_rebuildPending = false;
    };
}
