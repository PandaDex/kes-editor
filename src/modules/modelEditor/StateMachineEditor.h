#pragma once

#include <QGraphicsItem>
#include <QGraphicsPathItem>
#include <QGraphicsScene>
#include <QHash>
#include <QPointer>
#include <QWidget>
#include <functional>

class QVBoxLayout;

namespace nodegraph
{
    class NodeGraphView;
}

namespace kme
{

    class StateMachineModel;
    class StateMachineScene;

    class StateItem : public QGraphicsItem
    {
    public:
        enum
        {
            Type = QGraphicsItem::UserType + 20
        };
        static constexpr qreal kWidth = 150;
        static constexpr qreal kHeight = 46;

        StateItem(StateMachineScene* scene, int stateId);
        int type() const override { return Type; }
        int stateId() const { return m_id; }

        void setLabel(const QString& name, bool entry);
        void setWeight(float weight);

        QRectF boundingRect() const override;
        void paint(QPainter* p, const QStyleOptionGraphicsItem*, QWidget*) override;

    protected:
        QVariant itemChange(GraphicsItemChange change, const QVariant& value) override;
        void mousePressEvent(QGraphicsSceneMouseEvent* e) override;
        void mouseMoveEvent(QGraphicsSceneMouseEvent* e) override;
        void mouseReleaseEvent(QGraphicsSceneMouseEvent* e) override;

    private:
        StateMachineScene* m_scene;
        int m_id;
        QString m_name;
        bool m_entry = false;
        float m_weight = -1.0f;
        bool m_linking = false;
        QPointF m_pressPos;
    };

    class TransitionItem : public QGraphicsPathItem
    {
    public:
        enum
        {
            Type = QGraphicsItem::UserType + 21
        };

        TransitionItem(int transitionId, StateItem* from, StateItem* to, bool offset);
        int type() const override { return Type; }
        int transitionId() const { return m_id; }
        void updatePath();

        QPainterPath shape() const override { return m_shape; }
        void paint(QPainter* p, const QStyleOptionGraphicsItem*, QWidget*) override;

    private:
        int m_id;
        StateItem* m_from;
        StateItem* m_to;
        bool m_offset;
        QPainterPath m_shape;
        QPointF m_arrowTip;
        QPointF m_arrowDir;
    };

    class StateMachineScene : public QGraphicsScene
    {
        Q_OBJECT
    public:
        explicit StateMachineScene(StateMachineModel* machine, QObject* parent = nullptr);

        StateMachineModel* machine() const { return m_machine; }
        StateItem* stateItem(int id) const { return m_states.value(id); }
        QList<int> selectedStates() const;
        QList<int> selectedTransitions() const;
        void selectState(int id);

        int addStateAt(const QPointF& pos);
        void deleteSelection();
        void selectAllStates();
        void beginLink(int fromState, const QPointF& scenePos);
        void updateLink(const QPointF& scenePos);
        void finishLink(const QPointF& scenePos);
        void cancelLink();
        bool isLinking() const { return m_linkFrom != kNoLink; }
        void updateTransitions();
        void commitMoves();
        // nullptr hides the weight bars
        void setStateWeights(const std::function<float(int stateId)>& weightOf);

    signals:
        void stateOpened(int stateId);
        void selectionIdsChanged();
        void statusMessage(const QString& text);

    protected:
        void mouseMoveEvent(QGraphicsSceneMouseEvent* e) override;
        void mousePressEvent(QGraphicsSceneMouseEvent* e) override;
        void mouseDoubleClickEvent(QGraphicsSceneMouseEvent* e) override;
        void keyPressEvent(QKeyEvent* e) override;

    private:
        static constexpr int kNoLink = -1000;
        void scheduleRebuild();
        void rebuild();
        StateItem* stateAt(const QPointF& scenePos) const;

        StateMachineModel* m_machine;
        QHash<int, StateItem*> m_states;
        QList<TransitionItem*> m_transitions;
        QGraphicsPathItem* m_linkLine;
        int m_linkFrom = kNoLink;
        bool m_rebuildQueued = false;
        // selected once the next rebuild has made the item
        int m_selectStateAfter = 0;
        int m_selectTransitionAfter = 0;
    };

    // Properties of the selected state or transition.
    class StateMachineInspector : public QWidget
    {
        Q_OBJECT
    public:
        StateMachineInspector(StateMachineScene* scene, QWidget* parent = nullptr);
        void rebuild();

    signals:
        void openStateRequested(int stateId);

    private:
        void buildState(QVBoxLayout* lay, int stateId);
        void buildTransition(QVBoxLayout* lay, int transitionId);
        void scheduleRebuild();

        StateMachineScene* m_scene;
        QVBoxLayout* m_root;
        QWidget* m_content = nullptr;
        bool m_applying = false;
        bool m_rebuildQueued = false;
    };

    // Canvas, scene and inspector for one state machine.
    class StateMachineEditor : public QObject
    {
        Q_OBJECT
    public:
        // The owner deletes the view and inspector (they live in its layouts).
        StateMachineEditor(StateMachineModel* machine, QWidget* widgetParent);

        StateMachineModel* machine() const { return m_machine; }
        StateMachineScene* scene() const { return m_scene; }
        nodegraph::NodeGraphView* view() const { return m_view; }
        StateMachineInspector* inspector() const { return m_inspector; }

    signals:
        void stateOpened(int stateId);
        void statusMessage(const QString& text);

    private:
        void showContextMenu(const QPointF& scenePos, const QPoint& globalPos);

        StateMachineModel* m_machine;
        StateMachineScene* m_scene;
        nodegraph::NodeGraphView* m_view;
        StateMachineInspector* m_inspector;
    };

} // namespace kme
