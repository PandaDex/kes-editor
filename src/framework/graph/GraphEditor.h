#pragma once

#include <QObject>
#include <QPointF>
#include <QWidget>

class QAction;
class QLineEdit;
class QTreeWidget;

namespace nodegraph
{
    class GraphDomain;
    class GraphModel;
    class InspectorWidget;
    class NodeGraphScene;
    class NodeGraphView;
    class NodeSearchPopup;

    // Node list grouped by category; drag onto the canvas or double-click to add.
    class NodeLibraryPanel : public QWidget
    {
        Q_OBJECT
    public:
        explicit NodeLibraryPanel(const GraphDomain* domain, QWidget* parent = nullptr);

    signals:
        void nodeActivated(const QString& type);

    private:
        void filter(const QString& text);

        QLineEdit* m_search;
        QTreeWidget* m_tree;
    };

    // The parts every graph editor needs, wired together: canvas, add-node popup, inspector,
    // node library and the usual edit/view actions. The owner lays the widgets out.
    class GraphEditor : public QObject
    {
        Q_OBJECT
    public:
        struct Actions
        {
            QAction *cut, *copy, *paste, *duplicate, *remove, *selectAll, *addNode;
            QAction *frameAll, *frameSelection, *zoomIn, *zoomOut, *zoomReset, *snap;
        };

        // widgetParent owns the widgets until they are put into a layout or dock
        GraphEditor(GraphModel* model, QWidget* widgetParent);

        GraphModel* model() const { return m_model; }
        NodeGraphScene* scene() const { return m_scene; }
        NodeGraphView* view() const { return m_view; }
        InspectorWidget* inspector() const { return m_inspector; }
        NodeLibraryPanel* library() const { return m_library; }

        // Shortcuts are window-wide; add the actions to the owning window or a menu.
        Actions createActions(QObject* parent);
        void showAddNodePopup();

    signals:
        void statusMessage(const QString& text);

    private:
        GraphModel* m_model;
        NodeGraphScene* m_scene;
        NodeGraphView* m_view;
        NodeSearchPopup* m_popup;
        InspectorWidget* m_inspector;
        NodeLibraryPanel* m_library;

        QPointF m_popupPos;
        int m_pendingFromNode = -1;
        int m_pendingFromPort = -1;
        bool m_pendingFromIsOutput = false;
    };
}
