#include "framework/graph/GraphEditor.h"

#include "framework/graph/GraphModel.h"
#include "framework/graph/InspectorWidget.h"
#include "framework/graph/NodeGraphScene.h"
#include "framework/graph/NodeGraphView.h"
#include "framework/graph/NodeSearchPopup.h"
#include "ui/KesStyle.h"

#include <QAction>
#include <QCursor>
#include <QLabel>
#include <QLineEdit>
#include <QMimeData>
#include <QShortcut>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace nodegraph
{
    namespace
    {
        class LibraryTree : public QTreeWidget
        {
        public:
            using QTreeWidget::QTreeWidget;

        protected:
            QStringList mimeTypes() const override { return { NodeGraphView::kNodeMime }; }
            QMimeData* mimeData(const QList<QTreeWidgetItem*>& items) const override
            {
                auto* m = new QMimeData;
                for (QTreeWidgetItem* it : items)
                {
                    const QString type = it->data(0, Qt::UserRole).toString();
                    if (!type.isEmpty())
                    {
                        m->setData(NodeGraphView::kNodeMime, type.toUtf8());
                        break;
                    }
                }
                return m;
            }
        };
    }

    NodeLibraryPanel::NodeLibraryPanel(const GraphDomain* domain, QWidget* parent)
        : QWidget(parent)
    {
        auto* lay = new QVBoxLayout(this);
        lay->setContentsMargins(6, 6, 6, 6);
        m_search = new QLineEdit;
        m_search->setPlaceholderText(QString::fromUtf8("Filter nodes…"));
        m_search->addAction(kes::style::icon("search"), QLineEdit::LeadingPosition);
        m_search->setClearButtonEnabled(true);
        m_tree = new LibraryTree;
        m_tree->setHeaderHidden(true);
        m_tree->setDragEnabled(true);
        m_tree->setDragDropMode(QAbstractItemView::DragOnly);
        m_tree->setIndentation(14);
        lay->addWidget(m_search);
        lay->addWidget(m_tree);
        auto* hint = new QLabel("Double-click or drag onto the canvas.");
        hint->setObjectName("Muted");
        lay->addWidget(hint);

        const NodeCatalog& catalog = domain->catalog();
        for (const QString& cat : catalog.categories())
        {
            auto* catItem = new QTreeWidgetItem(m_tree, { cat });
            catItem->setFlags(Qt::ItemIsEnabled);
            QFont f = catItem->font(0);
            f.setBold(true);
            catItem->setFont(0, f);
            catItem->setForeground(0, domain->categoryColor(cat).lighter(140));
            for (const NodeDef& d : catalog.all())
            {
                if (d.category != cat) continue;
                auto* it = new QTreeWidgetItem(catItem, { d.title });
                it->setData(0, Qt::UserRole, d.type);
                it->setToolTip(0, d.description);
                it->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsDragEnabled);
            }
            catItem->setExpanded(true);
        }

        connect(m_search, &QLineEdit::textChanged, this, &NodeLibraryPanel::filter);
        connect(m_tree, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem* it)
            {
            const QString type = it->data(0, Qt::UserRole).toString();
            if (!type.isEmpty()) emit nodeActivated(type); });
    }

    void NodeLibraryPanel::filter(const QString& text)
    {
        const QStringList words = text.toLower().split(' ', Qt::SkipEmptyParts);
        for (int c = 0; c < m_tree->topLevelItemCount(); ++c)
        {
            QTreeWidgetItem* cat = m_tree->topLevelItem(c);
            int visible = 0;
            for (int i = 0; i < cat->childCount(); ++i)
            {
                QTreeWidgetItem* it = cat->child(i);
                const QString hay = (it->text(0) + " " + cat->text(0) + " " + it->toolTip(0)).toLower();
                bool match = true;
                for (const QString& w : words)
                    if (!hay.contains(w))
                    {
                        match = false;
                        break;
                    }
                it->setHidden(!match);
                if (match) ++visible;
            }
            cat->setHidden(visible == 0);
            if (!words.isEmpty()) cat->setExpanded(true);
        }
    }

    GraphEditor::GraphEditor(GraphModel* model, QWidget* widgetParent)
        : QObject(widgetParent)
        , m_model(model)
    {
        m_scene = new NodeGraphScene(model, this);
        m_view = new NodeGraphView(m_scene, widgetParent);
        m_popup = new NodeSearchPopup(&model->domain(), widgetParent);
        m_inspector = new InspectorWidget(model, widgetParent);
        m_library = new NodeLibraryPanel(&model->domain(), widgetParent);

        connect(m_scene, &NodeGraphScene::selectionIdsChanged, m_inspector, &InspectorWidget::setSelection);
        connect(m_scene, &NodeGraphScene::statusMessage, this, &GraphEditor::statusMessage);
        connect(m_view, &NodeGraphView::nodeDropped, this, [this](const QString& type, const QPointF& pos)
            { m_scene->addNodeAt(type, pos); });
        connect(m_library, &NodeLibraryPanel::nodeActivated, this, [this](const QString& type)
            { m_scene->addNodeAt(type, m_view->viewCenterScene()); });
        connect(m_view, &NodeGraphView::contextMenuRequestedAt, this, [this](const QPointF& scenePos, const QPoint& global)
            {
            m_popupPos = scenePos;
            m_pendingFromNode = -1;
            m_popup->popup(global); });
        connect(m_scene, &NodeGraphScene::nodePopupRequested, this, [this](const QPointF& scenePos, int fromNode, int fromPort, bool fromIsOutput)
            {
            m_popupPos = scenePos;
            m_pendingFromNode = fromNode;
            m_pendingFromPort = fromPort;
            m_pendingFromIsOutput = fromIsOutput;
            m_popup->popup(QCursor::pos()); });
        connect(m_popup, &NodeSearchPopup::nodeChosen, this, [this](const QString& type)
            {
            if (m_pendingFromNode >= 0)
                m_scene->createNodeAndConnect(type, m_popupPos, m_pendingFromNode, m_pendingFromPort, m_pendingFromIsOutput);
            else
                m_scene->addNodeAt(type, m_popupPos);
            m_pendingFromNode = -1;
            m_view->setFocus(); });

        auto* backspace = new QShortcut(QKeySequence(Qt::Key_Backspace), m_view, [this]
            { m_scene->deleteSelection(); });
        backspace->setContext(Qt::WidgetWithChildrenShortcut);
        auto* tab = new QShortcut(QKeySequence(Qt::Key_Tab), m_view, [this]
            { showAddNodePopup(); });
        tab->setContext(Qt::WidgetShortcut);
    }

    void GraphEditor::showAddNodePopup()
    {
        m_popupPos = m_view->cursorScenePos();
        m_pendingFromNode = -1;
        QWidget* vp = m_view->viewport();
        const QPoint local = vp->mapFromGlobal(QCursor::pos());
        const QPoint global = vp->rect().contains(local) ? QCursor::pos() : vp->mapToGlobal(vp->rect().center());
        m_popup->popup(global);
    }

    GraphEditor::Actions GraphEditor::createActions(QObject* parent)
    {
        auto make = [parent](const QString& text, const QKeySequence& ks, auto slot, const QString& icon = {})
        {
            auto* a = new QAction(text, parent);
            if (!ks.isEmpty()) a->setShortcut(ks);
            if (!icon.isEmpty()) a->setIcon(kes::style::icon(icon));
            QObject::connect(a, &QAction::triggered, parent, slot);
            return a;
        };

        Actions a{};
        a.cut = make("Cu&t", QKeySequence::Cut, [this]
            { m_scene->cutSelection(); });
        a.copy = make("&Copy", QKeySequence::Copy, [this]
            { m_scene->copySelection(); }, "copy");
        a.paste = make("&Paste", QKeySequence::Paste, [this]
            { m_scene->paste(m_view->cursorScenePos()); });
        a.duplicate = make("&Duplicate", QKeySequence("Ctrl+D"), [this]
            { m_scene->duplicateSelection(); });
        a.remove = make("De&lete", QKeySequence::Delete, [this]
            { m_scene->deleteSelection(); }, "trash");
        a.selectAll = make("Select &All", QKeySequence::SelectAll, [this]
            { m_scene->selectNodes(m_model->nodeIds()); });
        a.addNode = make(QString::fromUtf8("&Add Node…"), QKeySequence("Shift+A"), [this]
            { showAddNodePopup(); }, "plus");
        a.frameAll = make("Frame &All", QKeySequence(Qt::Key_F), [this]
            { m_view->frameAll(); });
        a.frameSelection = make("Frame &Selection", QKeySequence("Shift+F"), [this]
            { m_view->frameSelection(); });
        a.zoomIn = make("Zoom &In", QKeySequence::ZoomIn, [this]
            { m_view->zoomBy(1.2); });
        a.zoomOut = make("Zoom &Out", QKeySequence::ZoomOut, [this]
            { m_view->zoomBy(1 / 1.2); });
        a.zoomReset = make("&Reset Zoom", QKeySequence("Ctrl+0"), [this]
            { m_view->resetZoom(); });
        a.snap = new QAction("Snap to &Grid", parent);
        a.snap->setCheckable(true);
        connect(a.snap, &QAction::toggled, this, [this](bool on)
            { m_scene->setSnapToGrid(on); });
        return a;
    }
}
