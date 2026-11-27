#include "StateMachineEditor.h"

#include "AnimGraphDocument.h"
#include "framework/graph/NodeGraphView.h"
#include "ui/KesStyle.h"

#include <QCheckBox>
#include <QComboBox>
#include <QCursor>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGraphicsSceneMouseEvent>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPainter>
#include <QPainterPathStroker>
#include <QPushButton>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

namespace kme
{

    namespace
    {

        constexpr AnimConditionOp kOps[] = { AnimConditionOp::IsTrue, AnimConditionOp::IsFalse, AnimConditionOp::Greater,
            AnimConditionOp::Less, AnimConditionOp::GreaterEqual, AnimConditionOp::LessEqual, AnimConditionOp::Equal,
            AnimConditionOp::NotEqual, AnimConditionOp::Triggered };

        QString opLabel(AnimConditionOp op)
        {
            switch (op)
            {
            case AnimConditionOp::IsTrue:       return QStringLiteral("is true");
            case AnimConditionOp::IsFalse:      return QStringLiteral("is false");
            case AnimConditionOp::Greater:      return QStringLiteral(">");
            case AnimConditionOp::Less:         return QStringLiteral("<");
            case AnimConditionOp::GreaterEqual: return QStringLiteral(">=");
            case AnimConditionOp::LessEqual:    return QStringLiteral("<=");
            case AnimConditionOp::Equal:        return QStringLiteral("==");
            case AnimConditionOp::NotEqual:     return QStringLiteral("!=");
            case AnimConditionOp::Triggered:    return QStringLiteral("triggered");
            }
            return {};
        }

        bool opTakesValue(AnimConditionOp op)
        {
            return op != AnimConditionOp::IsTrue && op != AnimConditionOp::IsFalse && op != AnimConditionOp::Triggered;
        }

        QString conditionText(const AnimTransition& t)
        {
            QStringList parts;
            for (const AnimCondition& c : t.conditions)
                parts << (opTakesValue(c.op) ? QString("%1 %2 %3").arg(c.parameter, opLabel(c.op)).arg(c.value)
                                             : QString("%1 %2").arg(c.parameter, opLabel(c.op)));
            if (t.useExitTime) parts << QString("exit time %1").arg(t.exitTime);
            return parts.isEmpty() ? QStringLiteral("Never fires") : parts.join(QStringLiteral(" and "));
        }

        // Where the segment from the rectangle's centre towards `toward` leaves the rectangle.
        QPointF rectExit(const QRectF& r, const QPointF& toward)
        {
            const QPointF c = r.center();
            const QPointF d = toward - c;
            if (std::abs(d.x()) < 1e-6 && std::abs(d.y()) < 1e-6) return c;
            const double sx = std::abs(d.x()) > 1e-6 ? (r.width() / 2) / std::abs(d.x()) : 1e9;
            const double sy = std::abs(d.y()) > 1e-6 ? (r.height() / 2) / std::abs(d.y()) : 1e9;
            return c + d * std::min({ sx, sy, 1.0 });
        }

    } // namespace

    // ---------------------------------------------------------------------------------------------

    StateItem::StateItem(StateMachineScene* scene, int stateId)
        : m_scene(scene)
        , m_id(stateId)
    {
        setFlags(ItemIsMovable | ItemIsSelectable | ItemSendsGeometryChanges);
        setZValue(1);
        setAcceptHoverEvents(true);
    }

    void StateItem::setLabel(const QString& name, bool entry)
    {
        m_name = name;
        m_entry = entry;
        setToolTip(m_id == StateMachineModel::AnyState ? QStringLiteral("Transitions from here can fire in any state")
                   : entry                              ? QStringLiteral("Entry state")
                                                        : QString());
        update();
    }

    void StateItem::setWeight(float weight)
    {
        if (std::abs(weight - m_weight) < 1e-4f) return;
        m_weight = weight;
        update();
    }

    QRectF StateItem::boundingRect() const
    {
        return QRectF(-kWidth / 2 - 4, -kHeight / 2 - 4, kWidth + 8, kHeight + 8);
    }

    void StateItem::paint(QPainter* p, const QStyleOptionGraphicsItem*, QWidget*)
    {
        const QRectF r(-kWidth / 2, -kHeight / 2, kWidth, kHeight);
        const bool any = m_id == StateMachineModel::AnyState;
        const bool active = m_weight > 0.999f;
        p->setRenderHint(QPainter::Antialiasing);

        QColor fill = any ? kes::style::window() : kes::style::panel();
        if (active) fill = fill.lighter(125);
        QColor border = kes::style::border();
        qreal width = 1.2;
        if (m_entry) border = kes::style::accent();
        if (active) border = kes::style::info();
        if (isSelected())
        {
            border = kes::style::text();
            width = 2.2;
        }
        p->setPen(QPen(border, m_entry || active ? std::max(width, 2.0) : width, any ? Qt::DashLine : Qt::SolidLine));
        p->setBrush(fill);
        p->drawRoundedRect(r, 7, 7);

        if (m_entry)
        {
            QPainterPath tri;
            tri.moveTo(r.left() + 9, -6);
            tri.lineTo(r.left() + 17, 0);
            tri.lineTo(r.left() + 9, 6);
            tri.closeSubpath();
            p->fillPath(tri, kes::style::accent());
        }

        QFont f = p->font();
        f.setPointSizeF(10);
        f.setItalic(any);
        f.setBold(!any);
        p->setFont(f);
        p->setPen(any ? kes::style::muted() : kes::style::text());
        p->drawText(r.adjusted(20, 0, -10, 0), Qt::AlignCenter, p->fontMetrics().elidedText(m_name, Qt::ElideRight, int(r.width() - 30)));

        if (m_weight >= 0.0f && !any)
        {
            const QRectF bar(r.left() + 8, r.bottom() - 7, r.width() - 16, 3);
            p->fillRect(bar, QColor(255, 255, 255, 20));
            p->fillRect(QRectF(bar.left(), bar.top(), bar.width() * std::clamp(m_weight, 0.0f, 1.0f), bar.height()), kes::style::info());
        }
    }

    QVariant StateItem::itemChange(GraphicsItemChange change, const QVariant& value)
    {
        if (change == ItemPositionHasChanged) m_scene->updateTransitions();
        return QGraphicsItem::itemChange(change, value);
    }

    void StateItem::mousePressEvent(QGraphicsSceneMouseEvent* e)
    {
        if (e->button() == Qt::LeftButton && (e->modifiers() & Qt::ShiftModifier))
        {
            m_linking = true;
            m_scene->beginLink(m_id, e->scenePos());
            e->accept();
            return;
        }
        m_pressPos = pos();
        QGraphicsItem::mousePressEvent(e);
    }

    void StateItem::mouseMoveEvent(QGraphicsSceneMouseEvent* e)
    {
        if (m_linking)
        {
            m_scene->updateLink(e->scenePos());
            return;
        }
        QGraphicsItem::mouseMoveEvent(e);
    }

    void StateItem::mouseReleaseEvent(QGraphicsSceneMouseEvent* e)
    {
        if (m_linking)
        {
            m_linking = false;
            m_scene->finishLink(e->scenePos());
            return;
        }
        QGraphicsItem::mouseReleaseEvent(e);
        if (pos() != m_pressPos) m_scene->commitMoves();
    }

    // ---------------------------------------------------------------------------------------------

    TransitionItem::TransitionItem(int transitionId, StateItem* from, StateItem* to, bool offset)
        : m_id(transitionId)
        , m_from(from)
        , m_to(to)
        , m_offset(offset)
    {
        setFlags(ItemIsSelectable);
        setZValue(0);
        updatePath();
    }

    void TransitionItem::updatePath()
    {
        QPainterPath path;
        const QRectF ra = m_from->sceneBoundingRect().adjusted(4, 4, -4, -4);
        if (m_from == m_to)
        {
            const QPointF top(ra.center().x(), ra.top());
            const QPointF a = top + QPointF(-22, 0), b = top + QPointF(22, 0);
            path.moveTo(a);
            path.cubicTo(a + QPointF(-28, -56), b + QPointF(28, -56), b);
            m_arrowTip = path.pointAtPercent(0.5);
            m_arrowDir = QPointF(1, 0);
        }
        else
        {
            const QRectF rb = m_to->sceneBoundingRect().adjusted(4, 4, -4, -4);
            const QPointF ca = ra.center(), cb = rb.center();
            QPointF dir = cb - ca;
            const double len = std::hypot(dir.x(), dir.y());
            if (len > 1e-6) dir /= len;
            // a pair of opposite transitions sits side by side
            const QPointF shift = m_offset ? QPointF(-dir.y(), dir.x()) * 8 : QPointF();
            const QPointF a = rectExit(ra, cb) + shift;
            const QPointF b = rectExit(rb, ca) + shift;
            path.moveTo(a);
            path.lineTo(b);
            m_arrowTip = (a + b) / 2 + dir * 7;
            m_arrowDir = dir;
        }
        setPath(path);
        QPainterPathStroker stroker;
        stroker.setWidth(12);
        m_shape = stroker.createStroke(path);
        m_shape.addEllipse(m_arrowTip, 10, 10);
        update();
    }

    void TransitionItem::paint(QPainter* p, const QStyleOptionGraphicsItem*, QWidget*)
    {
        p->setRenderHint(QPainter::Antialiasing);
        const QColor c = isSelected() ? kes::style::accent() : QColor(200, 200, 205);
        p->setPen(QPen(c, isSelected() ? 2.6 : 1.8, Qt::SolidLine, Qt::RoundCap));
        p->setBrush(Qt::NoBrush);
        p->drawPath(path());

        const QPointF d = m_arrowDir;
        const QPointF n(-d.y(), d.x());
        QPainterPath head;
        head.moveTo(m_arrowTip);
        head.lineTo(m_arrowTip - d * 12 + n * 6);
        head.lineTo(m_arrowTip - d * 12 - n * 6);
        head.closeSubpath();
        p->fillPath(head, c);
    }

    // ---------------------------------------------------------------------------------------------

    StateMachineScene::StateMachineScene(StateMachineModel* machine, QObject* parent)
        : QGraphicsScene(parent)
        , m_machine(machine)
    {
        setSceneRect(-60000, -60000, 120000, 120000);
        setItemIndexMethod(QGraphicsScene::NoIndex);
        m_linkLine = new QGraphicsPathItem;
        m_linkLine->setZValue(10);
        m_linkLine->setAcceptedMouseButtons(Qt::NoButton);
        m_linkLine->setPen(QPen(QColor("#ffffff"), 2.0, Qt::DashLine, Qt::RoundCap));
        m_linkLine->setVisible(false);
        addItem(m_linkLine);

        connect(m_machine, &StateMachineModel::changed, this, &StateMachineScene::scheduleRebuild);
        connect(this, &QGraphicsScene::selectionChanged, this, &StateMachineScene::selectionIdsChanged);
        rebuild();
    }

    void StateMachineScene::scheduleRebuild()
    {
        if (m_rebuildQueued) return;
        m_rebuildQueued = true;
        QTimer::singleShot(0, this, [this]
            {
            m_rebuildQueued = false;
            rebuild(); });
    }

    void StateMachineScene::rebuild()
    {
        const QList<int> selStates = selectedStates();
        const QList<int> selTransitions = selectedTransitions();
        const QSignalBlocker block(this);

        for (TransitionItem* t : std::as_const(m_transitions))
        {
            removeItem(t);
            delete t;
        }
        m_transitions.clear();
        for (StateItem* s : std::as_const(m_states))
        {
            removeItem(s);
            delete s;
        }
        m_states.clear();

        auto* any = new StateItem(this, StateMachineModel::AnyState);
        any->setLabel(QStringLiteral("Any State"), false);
        any->setPos(m_machine->anyStatePos());
        addItem(any);
        m_states.insert(StateMachineModel::AnyState, any);
        for (const AnimState& s : m_machine->states())
        {
            auto* item = new StateItem(this, s.id);
            item->setLabel(s.name, s.id == m_machine->entryState());
            item->setPos(s.pos);
            addItem(item);
            m_states.insert(s.id, item);
        }
        for (const AnimTransition& t : m_machine->transitions())
        {
            StateItem* from = m_states.value(t.from);
            StateItem* to = m_states.value(t.to);
            if (!from || !to) continue;
            const bool reverse = std::any_of(m_machine->transitions().begin(), m_machine->transitions().end(), [&](const AnimTransition& o)
                { return o.from == t.to && o.to == t.from && t.from != t.to; });
            auto* item = new TransitionItem(t.id, from, to, reverse);
            item->setToolTip(QString("%1 → %2\n%3").arg(m_machine->stateName(t.from), m_machine->stateName(t.to), conditionText(t)));
            addItem(item);
            m_transitions << item;
        }

        const bool replace = m_selectStateAfter != 0 || m_selectTransitionAfter != 0;
        for (int id : selStates)
            if (StateItem* s = m_states.value(id); s && (!replace || id == m_selectStateAfter)) s->setSelected(true);
        if (StateItem* s = m_states.value(m_selectStateAfter)) s->setSelected(true);
        for (TransitionItem* t : std::as_const(m_transitions))
            if ((!replace && selTransitions.contains(t->transitionId())) || t->transitionId() == m_selectTransitionAfter) t->setSelected(true);
        m_selectStateAfter = 0;
        m_selectTransitionAfter = 0;
        emit selectionIdsChanged();
    }

    QList<int> StateMachineScene::selectedStates() const
    {
        QList<int> r;
        for (QGraphicsItem* item : selectedItems())
            if (item->type() == StateItem::Type) r << static_cast<StateItem*>(item)->stateId();
        return r;
    }

    QList<int> StateMachineScene::selectedTransitions() const
    {
        QList<int> r;
        for (QGraphicsItem* item : selectedItems())
            if (item->type() == TransitionItem::Type) r << static_cast<TransitionItem*>(item)->transitionId();
        return r;
    }

    void StateMachineScene::selectState(int id)
    {
        clearSelection();
        if (StateItem* s = m_states.value(id)) s->setSelected(true);
    }

    void StateMachineScene::selectAllStates()
    {
        for (StateItem* s : std::as_const(m_states))
            s->setSelected(true);
    }

    int StateMachineScene::addStateAt(const QPointF& pos)
    {
        int id = 0;
        m_machine->document()->edit("Add state", [&]
            { id = m_machine->addState(pos, QStringLiteral("State")); });
        m_selectStateAfter = id;
        scheduleRebuild();
        return id;
    }

    void StateMachineScene::deleteSelection()
    {
        const QList<int> states = selectedStates();
        const QList<int> transitions = selectedTransitions();
        if (states.isEmpty() && transitions.isEmpty()) return;
        m_machine->document()->edit("Delete", [&]
            {
            for (int id : transitions) m_machine->removeTransition(id);
            for (int id : states)
                if (id != StateMachineModel::AnyState) m_machine->removeState(id); });
    }

    void StateMachineScene::beginLink(int fromState, const QPointF& scenePos)
    {
        m_linkFrom = fromState;
        m_linkLine->setVisible(true);
        updateLink(scenePos);
        emit statusMessage("Click the state the transition goes to (Esc cancels).");
    }

    void StateMachineScene::updateLink(const QPointF& scenePos)
    {
        StateItem* from = m_states.value(m_linkFrom);
        if (!from) return;
        QPainterPath path;
        path.moveTo(from->scenePos());
        path.lineTo(scenePos);
        m_linkLine->setPath(path);
    }

    void StateMachineScene::finishLink(const QPointF& scenePos)
    {
        const int from = m_linkFrom;
        cancelLink();
        StateItem* target = stateAt(scenePos);
        if (!target || target->stateId() == StateMachineModel::AnyState)
        {
            emit statusMessage(target ? "A transition can't go to Any State." : QString());
            return;
        }
        int id = -1;
        m_machine->document()->edit("Add transition", [&]
            { id = m_machine->addTransition(from, target->stateId()); });
        m_selectTransitionAfter = std::max(id, 0);
        scheduleRebuild();
    }

    void StateMachineScene::cancelLink()
    {
        m_linkFrom = kNoLink;
        m_linkLine->setVisible(false);
    }

    StateItem* StateMachineScene::stateAt(const QPointF& scenePos) const
    {
        for (QGraphicsItem* item : items(scenePos))
            if (item->type() == StateItem::Type) return static_cast<StateItem*>(item);
        return nullptr;
    }

    void StateMachineScene::updateTransitions()
    {
        for (TransitionItem* t : std::as_const(m_transitions))
            t->updatePath();
    }

    void StateMachineScene::commitMoves()
    {
        QHash<int, QPointF> moved;
        for (StateItem* s : std::as_const(m_states))
            moved.insert(s->stateId(), s->pos());
        m_machine->document()->edit("Move states", [&]
            {
            for (auto it = moved.constBegin(); it != moved.constEnd(); ++it) {
                if (it.key() == StateMachineModel::AnyState) m_machine->setAnyStatePos(it.value());
                else m_machine->setStatePos(it.key(), it.value());
            } });
    }

    void StateMachineScene::setStateWeights(const std::function<float(int)>& weightOf)
    {
        for (StateItem* s : std::as_const(m_states))
            s->setWeight(weightOf && s->stateId() != StateMachineModel::AnyState ? weightOf(s->stateId()) : -1.0f);
    }

    void StateMachineScene::mouseMoveEvent(QGraphicsSceneMouseEvent* e)
    {
        if (isLinking()) updateLink(e->scenePos());
        QGraphicsScene::mouseMoveEvent(e);
    }

    void StateMachineScene::mousePressEvent(QGraphicsSceneMouseEvent* e)
    {
        if (isLinking())
        {
            if (e->button() == Qt::LeftButton)
                finishLink(e->scenePos());
            else
                cancelLink();
            e->accept();
            return;
        }
        QGraphicsScene::mousePressEvent(e);
    }

    void StateMachineScene::mouseDoubleClickEvent(QGraphicsSceneMouseEvent* e)
    {
        if (StateItem* s = stateAt(e->scenePos()); s && s->stateId() != StateMachineModel::AnyState)
        {
            emit stateOpened(s->stateId());
            e->accept();
            return;
        }
        QGraphicsScene::mouseDoubleClickEvent(e);
    }

    void StateMachineScene::keyPressEvent(QKeyEvent* e)
    {
        if (e->key() == Qt::Key_Escape && isLinking())
        {
            cancelLink();
            emit statusMessage(QString());
            e->accept();
            return;
        }
        QGraphicsScene::keyPressEvent(e);
    }

    // ---------------------------------------------------------------------------------------------

    StateMachineInspector::StateMachineInspector(StateMachineScene* scene, QWidget* parent)
        : QWidget(parent)
        , m_scene(scene)
    {
        m_root = new QVBoxLayout(this);
        m_root->setContentsMargins(8, 8, 8, 8);
        m_root->addStretch(1);
        connect(m_scene, &StateMachineScene::selectionIdsChanged, this, &StateMachineInspector::scheduleRebuild);
        connect(m_scene->machine(), &StateMachineModel::changed, this, [this]
            {
            if (!m_applying) scheduleRebuild(); });
        rebuild();
    }

    void StateMachineInspector::scheduleRebuild()
    {
        if (m_rebuildQueued) return;
        m_rebuildQueued = true;
        QTimer::singleShot(0, this, [this]
            {
            m_rebuildQueued = false;
            rebuild(); });
    }

    void StateMachineInspector::rebuild()
    {
        if (m_content)
        {
            m_root->removeWidget(m_content);
            m_content->deleteLater();
        }
        m_content = new QWidget;
        auto* lay = new QVBoxLayout(m_content);
        lay->setContentsMargins(0, 0, 0, 0);
        m_root->insertWidget(0, m_content);

        const QList<int> states = m_scene->selectedStates();
        const QList<int> transitions = m_scene->selectedTransitions();
        if (states.size() + transitions.size() == 1)
        {
            if (!states.isEmpty())
                buildState(lay, states.first());
            else
                buildTransition(lay, transitions.first());
            return;
        }
        auto* l = new QLabel(states.isEmpty() && transitions.isEmpty() ? "No selection" : "Several items selected");
        l->setObjectName("Muted");
        lay->addWidget(l);
    }

    void StateMachineInspector::buildState(QVBoxLayout* lay, int stateId)
    {
        StateMachineModel* m = m_scene->machine();
        if (stateId == StateMachineModel::AnyState)
        {
            auto* l = new QLabel("Any State");
            l->setObjectName("Muted");
            lay->addWidget(l);
            return;
        }
        const AnimState* s = m->state(stateId);
        if (!s) return;

        auto* form = new QFormLayout;
        form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
        auto* name = new QLineEdit(s->name);
        connect(name, &QLineEdit::editingFinished, this, [this, m, stateId, name]
            { m->document()->edit("Rename state", [&]
                  { m->setStateName(stateId, name->text()); }); });
        form->addRow("Name", name);

        auto* entry = new QCheckBox("Entry state");
        entry->setChecked(m->entryState() == stateId);
        entry->setEnabled(m->entryState() != stateId);
        connect(entry, &QCheckBox::toggled, this, [m, stateId](bool on)
            {
            if (on) m->document()->edit("Set entry state", [&] { m->setEntryState(stateId); }); });
        form->addRow(QString(), entry);
        lay->addLayout(form);

        auto* open = new QPushButton("Open State Graph");
        connect(open, &QPushButton::clicked, this, [this, stateId]
            { emit openStateRequested(stateId); });
        lay->addWidget(open);

        int out = 0, in = 0;
        for (const AnimTransition& t : m->transitions())
        {
            if (t.from == stateId) ++out;
            if (t.to == stateId) ++in;
        }
        auto* info = new QLabel(QString("%1 in, %2 out").arg(in).arg(out));
        info->setObjectName("Muted");
        lay->addWidget(info);
    }

    void StateMachineInspector::buildTransition(QVBoxLayout* lay, int transitionId)
    {
        StateMachineModel* m = m_scene->machine();
        const AnimTransition* found = m->transition(transitionId);
        if (!found) return;
        const AnimTransition t = *found;

        const auto apply = [this, m, transitionId](const QString& what, const std::function<void(AnimTransition&)>& change, bool rebuildAfter)
        {
            const AnimTransition* cur = m->transition(transitionId);
            if (!cur) return;
            AnimTransition copy = *cur;
            change(copy);
            m_applying = true;
            m->document()->edit(what, [&]
                { m->setTransition(copy); }, rebuildAfter ? QString() : QString("transition:%1:%2").arg(transitionId).arg(what));
            m_applying = false;
            if (rebuildAfter) scheduleRebuild();
        };

        auto* title = new QLabel(QString("<b>%1</b> → <b>%2</b>").arg(m->stateName(t.from).toHtmlEscaped(), m->stateName(t.to).toHtmlEscaped()));
        lay->addWidget(title);

        auto* form = new QFormLayout;
        form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
        auto* duration = new QDoubleSpinBox;
        duration->setRange(0.0, 10.0);
        duration->setDecimals(2);
        duration->setSingleStep(0.05);
        duration->setSuffix(" s");
        duration->setValue(t.duration);
        duration->setToolTip("Cross-fade time");
        connect(duration, &QDoubleSpinBox::valueChanged, this, [apply](double v)
            { apply("Blend", [v](AnimTransition& x)
                  { x.duration = v; }, false); });
        form->addRow("Blend", duration);

        auto* exitRow = new QWidget;
        auto* exitLay = new QHBoxLayout(exitRow);
        exitLay->setContentsMargins(0, 0, 0, 0);
        auto* useExit = new QCheckBox;
        useExit->setChecked(t.useExitTime);
        useExit->setToolTip("Wait until the state's main clip has played this far (0..1)");
        auto* exitTime = new QDoubleSpinBox;
        exitTime->setRange(0.0, 1.0);
        exitTime->setDecimals(2);
        exitTime->setSingleStep(0.05);
        exitTime->setValue(t.exitTime);
        exitTime->setEnabled(t.useExitTime);
        exitLay->addWidget(useExit);
        exitLay->addWidget(exitTime, 1);
        connect(useExit, &QCheckBox::toggled, this, [apply, exitTime](bool on)
            {
            exitTime->setEnabled(on);
            apply("Exit time", [on](AnimTransition& x) { x.useExitTime = on; }, true); });
        connect(exitTime, &QDoubleSpinBox::valueChanged, this, [apply](double v)
            { apply("Exit time value", [v](AnimTransition& x)
                  { x.exitTime = v; }, false); });
        form->addRow("Exit time", exitRow);
        lay->addLayout(form);

        auto* condTitle = new QLabel("<b>Conditions</b> (all must pass)");
        lay->addWidget(condTitle);
        const QStringList names = m->document()->parameterNames();
        for (int i = 0; i < t.conditions.size(); ++i)
        {
            const AnimCondition& c = t.conditions[i];
            auto* row = new QWidget;
            auto* h = new QHBoxLayout(row);
            h->setContentsMargins(0, 0, 0, 0);
            h->setSpacing(3);
            auto* param = new QComboBox;
            param->setEditable(true);
            param->addItems(names);
            param->setCurrentText(c.parameter);
            param->setMinimumContentsLength(8);
            param->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
            auto* op = new QComboBox;
            for (AnimConditionOp o : kOps)
                op->addItem(opLabel(o), int(o));
            op->setCurrentIndex(std::max(0, op->findData(int(c.op))));
            auto* value = new QDoubleSpinBox;
            value->setRange(-1e5, 1e5);
            value->setDecimals(2);
            value->setValue(c.value);
            value->setVisible(opTakesValue(c.op));
            auto* remove = new QToolButton;
            remove->setIcon(kes::style::icon("trash"));
            remove->setToolTip("Remove condition");
            h->addWidget(param, 2);
            h->addWidget(op, 1);
            h->addWidget(value, 1);
            h->addWidget(remove);
            lay->addWidget(row);

            connect(param, &QComboBox::currentTextChanged, this, [apply, i](const QString& text)
                { apply("Condition parameter", [&](AnimTransition& x)
                      { if (i < x.conditions.size()) x.conditions[i].parameter = text; }, false); });
            connect(op, &QComboBox::currentIndexChanged, this, [apply, i, op, value](int)
                {
                const auto o = AnimConditionOp(op->currentData().toInt());
                value->setVisible(opTakesValue(o));
                apply("Condition", [&](AnimTransition& x) { if (i < x.conditions.size()) x.conditions[i].op = o; }, false); });
            connect(value, &QDoubleSpinBox::valueChanged, this, [apply, i](double v)
                { apply("Condition value", [&](AnimTransition& x)
                      { if (i < x.conditions.size()) x.conditions[i].value = v; }, false); });
            connect(remove, &QToolButton::clicked, this, [apply, i]
                { apply("Remove condition", [&](AnimTransition& x)
                      { if (i < x.conditions.size()) x.conditions.removeAt(i); }, true); });
        }

        auto* add = new QPushButton(kes::style::icon("plus"), "Add Condition");
        connect(add, &QPushButton::clicked, this, [apply, m, names]
            {
            AnimCondition c;
            c.parameter = names.value(0);
            const AnimPortType type = m->document()->parameterType(c.parameter);
            c.op = c.parameter.isEmpty() || type == AnimPortType::Bool ? AnimConditionOp::IsTrue : AnimConditionOp::Greater;
            apply("Add condition", [&](AnimTransition& x) { x.conditions.append(c); }, true); });
        lay->addWidget(add);

        if (t.conditions.isEmpty() && !t.useExitTime)
        {
            auto* warn = new QLabel("Never fires: add a condition or an exit time.");
            warn->setObjectName("Warning");
            warn->setWordWrap(true);
            lay->addWidget(warn);
        }

        auto* order = new QWidget;
        auto* oh = new QHBoxLayout(order);
        oh->setContentsMargins(0, 6, 0, 0);
        auto* up = new QPushButton("Check Earlier");
        auto* down = new QPushButton("Check Later");
        up->setToolTip("Transitions are checked in order; the first that passes fires");
        down->setToolTip(up->toolTip());
        oh->addWidget(up);
        oh->addWidget(down);
        lay->addWidget(order);
        const auto move = [this, m, transitionId](int delta)
        {
            m->document()->edit("Reorder transition", [&]
                { m->moveTransition(transitionId, delta); });
            scheduleRebuild();
        };
        connect(up, &QPushButton::clicked, this, [move]
            { move(-1); });
        connect(down, &QPushButton::clicked, this, [move]
            { move(1); });
    }

    // ---------------------------------------------------------------------------------------------

    StateMachineEditor::StateMachineEditor(StateMachineModel* machine, QWidget* widgetParent)
        : QObject(widgetParent)
        , m_machine(machine)
    {
        m_scene = new StateMachineScene(machine, this);
        m_view = new nodegraph::NodeGraphView(m_scene, widgetParent);
        m_inspector = new StateMachineInspector(m_scene, widgetParent);
        connect(m_scene, &StateMachineScene::stateOpened, this, &StateMachineEditor::stateOpened);
        connect(m_inspector, &StateMachineInspector::openStateRequested, this, &StateMachineEditor::stateOpened);
        connect(m_scene, &StateMachineScene::statusMessage, this, &StateMachineEditor::statusMessage);
        connect(m_view, &nodegraph::NodeGraphView::contextMenuRequestedAt, this, &StateMachineEditor::showContextMenu);
    }

    void StateMachineEditor::showContextMenu(const QPointF& scenePos, const QPoint& globalPos)
    {
        QMenu menu;
        QGraphicsItem* hit = nullptr;
        for (QGraphicsItem* item : m_scene->items(scenePos))
            if (item->type() == StateItem::Type || item->type() == TransitionItem::Type)
            {
                hit = item;
                break;
            }

        if (hit && hit->type() == StateItem::Type)
        {
            const int id = static_cast<StateItem*>(hit)->stateId();
            if (!hit->isSelected()) m_scene->selectState(id);
            menu.addAction(kes::style::icon("plus"), "Make Transition", this, [this, id, scenePos]
                { m_scene->beginLink(id, scenePos); });
            if (id != StateMachineModel::AnyState)
            {
                menu.addAction("Open State Graph", this, [this, id]
                    { emit stateOpened(id); });
                QAction* entry = menu.addAction("Set as Entry State", this, [this, id]
                    { m_machine->document()->edit("Set entry state", [&]
                          { m_machine->setEntryState(id); }); });
                entry->setEnabled(m_machine->entryState() != id);
                menu.addSeparator();
                menu.addAction(kes::style::icon("trash"), "Delete", this, [this]
                    { m_scene->deleteSelection(); });
            }
        }
        else if (hit)
        {
            if (!hit->isSelected())
            {
                m_scene->clearSelection();
                hit->setSelected(true);
            }
            menu.addAction(kes::style::icon("trash"), "Delete Transition", this, [this]
                { m_scene->deleteSelection(); });
        }
        else
        {
            menu.addAction(kes::style::icon("plus"), "Add State", this, [this, scenePos]
                { m_scene->addStateAt(scenePos); });
        }
        menu.exec(globalPos);
    }

} // namespace kme
