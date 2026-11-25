#include "ValueEditors.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QLineEdit>
#include <QRegularExpressionValidator>
#include <QWheelEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
#include <functional>

namespace kpe
{
    namespace
    {
        // Compact number entry: commits on Enter or focus loss; the wheel steps it while it has focus.
        class NumberField : public QLineEdit
        {
        public:
            NumberField(double value, double step, std::function<void(double)> onChange)
                : m_step(step), m_onChange(std::move(onChange))
            {
                setValidator(new QRegularExpressionValidator(QRegularExpression(R"([-+]?[0-9]*[.,]?[0-9]*([eE][-+]?[0-9]+)?)"), this));
                setMinimumWidth(40);
                setValue(value);
                connect(this, &QLineEdit::editingFinished, this, [this] { commit(); });
            }

        protected:
            void wheelEvent(QWheelEvent* e) override
            {
                if (!hasFocus())
                {
                    e->ignore();
                    return;
                }
                const double steps = e->angleDelta().y() / 120.0;
                setValue(m_value + steps * m_step * ((e->modifiers() & Qt::ShiftModifier) ? 10.0 : 1.0));
                m_onChange(m_value);
                e->accept();
            }

        private:
            void setValue(double v)
            {
                m_value = v;
                setText(QString::number(v, 'g', 6));
                setCursorPosition(0);
            }
            void commit()
            {
                bool ok = false;
                const double v = QString(text()).replace(',', '.').toDouble(&ok);
                if (!ok)
                {
                    setValue(m_value);
                    return;
                }
                if (v == m_value)
                    return;
                setValue(v);
                m_onChange(v);
            }

            double m_value = 0.0;
            double m_step;
            std::function<void(double)> m_onChange;
        };

        QColor displayColor(const float c[4])
        {
            const float m = std::max({ c[0], c[1], c[2], 1.0f });
            return QColor::fromRgbF(std::clamp(c[0] / m, 0.0f, 1.0f), std::clamp(c[1] / m, 0.0f, 1.0f), std::clamp(c[2] / m, 0.0f, 1.0f),
                std::clamp(c[3], 0.0f, 1.0f));
        }

        void paintChecker(QPainter& p, const QRect& r)
        {
            const int s = 6;
            for (int y = r.top(); y < r.bottom(); y += s)
                for (int x = r.left(); x < r.right(); x += s)
                    p.fillRect(QRect(x, y, s, s).intersected(r), ((x - r.left()) / s + (y - r.top()) / s) % 2 ? QColor(70, 70, 74) : QColor(110, 110, 116));
        }

        const char* kModeNames[] = { "Constant", "Random", "Curve", "Gradient", "Graph" };
        const char* kInputNames[] = { "NormalizedAge", "Age", "Speed", "Random", "EmitterTime", "NormalizedLoopTime" };
    }

    // Curve

    CurveEdit::CurveEdit(QWidget* parent)
        : QWidget(parent)
    {
        setMinimumHeight(100);
        setMouseTracking(false);
        setToolTip("Drag keys; double-click to add one, right-click to remove one.");
    }

    void CurveEdit::setKeys(const std::vector<ParticleCurveKey>& keys)
    {
        m_keys = keys;
        if (m_keys.empty())
            m_keys = { { 0.0f, 1.0f }, { 1.0f, 1.0f } };
        update();
    }

    QRectF CurveEdit::plotRect() const
    {
        return QRectF(rect()).adjusted(28, 6, -8, -16);
    }

    void CurveEdit::range(float& x1, float& y0, float& y1) const
    {
        if (m_drag >= 0)
        {
            x1 = m_frozenRange[0];
            y0 = m_frozenRange[1];
            y1 = m_frozenRange[2];
            return;
        }
        x1 = 1.0f;
        y0 = 0.0f;
        y1 = 1.0f;
        for (const ParticleCurveKey& k : m_keys)
        {
            x1 = std::max(x1, k.time);
            y0 = std::min(y0, k.value);
            y1 = std::max(y1, k.value);
        }
        const float pad = (y1 - y0) * 0.1f;
        y1 += pad;
        if (y0 < 0.0f)
            y0 -= pad;
    }

    QPointF CurveEdit::toScreen(const ParticleCurveKey& k) const
    {
        float x1, y0, y1;
        range(x1, y0, y1);
        const QRectF r = plotRect();
        return { r.left() + r.width() * (k.time / x1), r.bottom() - r.height() * ((k.value - y0) / std::max(y1 - y0, 1e-6f)) };
    }

    ParticleCurveKey CurveEdit::fromScreen(const QPointF& p) const
    {
        float x1, y0, y1;
        range(x1, y0, y1);
        const QRectF r = plotRect();
        return { float((p.x() - r.left()) / r.width()) * x1, y0 + float((r.bottom() - p.y()) / r.height()) * (y1 - y0) };
    }

    int CurveEdit::hit(const QPointF& p) const
    {
        for (int i = 0; i < int(m_keys.size()); ++i)
            if (QLineF(toScreen(m_keys[size_t(i)]), p).length() < 7.0)
                return i;
        return -1;
    }

    void CurveEdit::paintEvent(QPaintEvent*)
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const QRectF r = plotRect();
        p.fillRect(rect(), QColor(30, 30, 34));
        p.fillRect(r, QColor(40, 40, 46));
        float x1, y0, y1;
        range(x1, y0, y1);
        p.setPen(QColor(150, 150, 156));
        QFont f = font();
        f.setPointSizeF(f.pointSizeF() * 0.8);
        p.setFont(f);
        p.drawText(QRectF(0, r.top() - 6, 26, 14), Qt::AlignRight | Qt::AlignVCenter, QString::number(y1, 'g', 3));
        p.drawText(QRectF(0, r.bottom() - 8, 26, 14), Qt::AlignRight | Qt::AlignVCenter, QString::number(y0, 'g', 3));
        p.drawText(QRectF(r.right() - 40, r.bottom() + 2, 40, 12), Qt::AlignRight, QString::number(x1, 'g', 3));
        if (y0 < 0.0f && y1 > 0.0f)
        {
            p.setPen(QColor(70, 70, 78));
            const double zy = toScreen({ 0.0f, 0.0f }).y();
            p.drawLine(QPointF(r.left(), zy), QPointF(r.right(), zy));
        }

        QPainterPath path;
        const int steps = 64;
        for (int i = 0; i <= steps; ++i)
        {
            const float x = x1 * float(i) / float(steps);
            const QPointF s = toScreen({ x, EvaluateParticleCurve(m_keys, x) });
            if (i == 0)
                path.moveTo(s);
            else
                path.lineTo(s);
        }
        p.setPen(QPen(QColor(110, 180, 255), 1.6));
        p.drawPath(path);
        p.setPen(Qt::NoPen);
        for (int i = 0; i < int(m_keys.size()); ++i)
        {
            p.setBrush(i == m_drag ? QColor(255, 210, 90) : QColor(230, 230, 236));
            p.drawEllipse(toScreen(m_keys[size_t(i)]), 4.0, 4.0);
        }
    }

    void CurveEdit::mousePressEvent(QMouseEvent* e)
    {
        const int i = hit(e->position());
        if (e->button() == Qt::RightButton)
        {
            if (i >= 0 && m_keys.size() > 1)
            {
                m_keys.erase(m_keys.begin() + i);
                update();
                emit keysChanged(m_keys);
            }
            return;
        }
        float x1, y0, y1;
        range(x1, y0, y1);
        m_frozenRange[0] = x1;
        m_frozenRange[1] = y0;
        m_frozenRange[2] = y1;
        m_drag = i;
        update();
    }

    void CurveEdit::mouseMoveEvent(QMouseEvent* e)
    {
        if (m_drag < 0)
            return;
        ParticleCurveKey k = fromScreen(e->position());
        const float lo = m_drag > 0 ? m_keys[size_t(m_drag) - 1].time : 0.0f;
        const float hi = m_drag + 1 < int(m_keys.size()) ? m_keys[size_t(m_drag) + 1].time : m_frozenRange[0];
        k.time = std::clamp(k.time, lo, hi);
        if (e->modifiers() & Qt::ShiftModifier)
            k.value = std::round(k.value * 20.0f) / 20.0f;
        m_keys[size_t(m_drag)] = k;
        update();
        emit keysChanged(m_keys);
    }

    void CurveEdit::mouseReleaseEvent(QMouseEvent*)
    {
        m_drag = -1;
        update();
    }

    void CurveEdit::mouseDoubleClickEvent(QMouseEvent* e)
    {
        ParticleCurveKey k = fromScreen(e->position());
        float x1, y0, y1;
        range(x1, y0, y1);
        k.time = std::clamp(k.time, 0.0f, x1);
        m_keys.push_back(k);
        std::sort(m_keys.begin(), m_keys.end(), [](const ParticleCurveKey& a, const ParticleCurveKey& b) { return a.time < b.time; });
        update();
        emit keysChanged(m_keys);
    }

    // Gradient

    class GradientEdit::Bar : public QWidget
    {
    public:
        explicit Bar(GradientEdit* owner)
            : QWidget(owner), m_owner(owner)
        {
            setMinimumHeight(34);
            setToolTip("Drag stops; double-click to add one, right-click to remove one.");
        }

    protected:
        QRectF barRect() const { return QRectF(rect()).adjusted(6, 2, -6, -12); }
        float timeAt(double x) const
        {
            const QRectF r = barRect();
            return std::clamp(float((x - r.left()) / r.width()), 0.0f, 1.0f);
        }
        int hit(const QPointF& p) const
        {
            const QRectF r = barRect();
            for (int i = 0; i < int(m_owner->m_keys.size()); ++i)
            {
                const double x = r.left() + r.width() * m_owner->m_keys[size_t(i)].time;
                if (std::abs(p.x() - x) < 6.0 && p.y() > r.bottom() - 4)
                    return i;
            }
            return -1;
        }

        void paintEvent(QPaintEvent*) override
        {
            QPainter p(this);
            const QRectF r = barRect();
            paintChecker(p, r.toRect());
            QLinearGradient g(r.topLeft(), r.topRight());
            for (const ParticleGradientKey& k : m_owner->m_keys)
                g.setColorAt(std::clamp(double(k.time), 0.0, 1.0), displayColor(k.color));
            p.fillRect(r, g);
            p.setRenderHint(QPainter::Antialiasing);
            for (int i = 0; i < int(m_owner->m_keys.size()); ++i)
            {
                const double x = r.left() + r.width() * m_owner->m_keys[size_t(i)].time;
                QPolygonF tri;
                tri << QPointF(x, r.bottom()) << QPointF(x - 5, r.bottom() + 10) << QPointF(x + 5, r.bottom() + 10);
                p.setPen(QPen(i == m_owner->m_selected ? QColor(255, 210, 90) : QColor(20, 20, 20), 1.2));
                QColor c = displayColor(m_owner->m_keys[size_t(i)].color);
                c.setAlpha(255);
                p.setBrush(c);
                p.drawPolygon(tri);
            }
        }

        void mousePressEvent(QMouseEvent* e) override
        {
            const int i = hit(e->position());
            if (i < 0)
                return;
            if (e->button() == Qt::RightButton)
            {
                if (m_owner->m_keys.size() > 1)
                {
                    m_owner->m_keys.erase(m_owner->m_keys.begin() + i);
                    m_owner->m_selected = 0;
                    m_owner->rebuildStopEditor();
                    update();
                    emit m_owner->keysChanged(m_owner->m_keys);
                }
                return;
            }
            m_drag = i;
            m_owner->m_selected = i;
            m_owner->rebuildStopEditor();
            update();
        }

        void mouseMoveEvent(QMouseEvent* e) override
        {
            if (m_drag < 0)
                return;
            m_owner->m_keys[size_t(m_drag)].time = timeAt(e->position().x());
            update();
            emit m_owner->keysChanged(m_owner->m_keys);
        }

        void mouseReleaseEvent(QMouseEvent*) override
        {
            if (m_drag >= 0)
            {
                // Keep stops sorted; follow the dragged one.
                const ParticleGradientKey moved = m_owner->m_keys[size_t(m_drag)];
                std::stable_sort(m_owner->m_keys.begin(), m_owner->m_keys.end(),
                    [](const ParticleGradientKey& a, const ParticleGradientKey& b) { return a.time < b.time; });
                for (int i = 0; i < int(m_owner->m_keys.size()); ++i)
                    if (m_owner->m_keys[size_t(i)].time == moved.time)
                        m_owner->m_selected = i;
                emit m_owner->keysChanged(m_owner->m_keys);
            }
            m_drag = -1;
            update();
        }

        void mouseDoubleClickEvent(QMouseEvent* e) override
        {
            ParticleGradientKey k;
            k.time = timeAt(e->position().x());
            EvaluateParticleGradient(m_owner->m_keys, k.time, k.color);
            m_owner->m_keys.push_back(k);
            std::sort(m_owner->m_keys.begin(), m_owner->m_keys.end(),
                [](const ParticleGradientKey& a, const ParticleGradientKey& b) { return a.time < b.time; });
            for (int i = 0; i < int(m_owner->m_keys.size()); ++i)
                if (m_owner->m_keys[size_t(i)].time == k.time)
                    m_owner->m_selected = i;
            m_owner->rebuildStopEditor();
            update();
            emit m_owner->keysChanged(m_owner->m_keys);
        }

    private:
        GradientEdit* m_owner;
        int m_drag = -1;
    };

    GradientEdit::GradientEdit(QWidget* parent)
        : QWidget(parent)
    {
        auto* layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(2);
        m_bar = new Bar(this);
        layout->addWidget(m_bar);
        m_stopEditor = new QWidget;
        layout->addWidget(m_stopEditor);
    }

    void GradientEdit::setKeys(const std::vector<ParticleGradientKey>& keys)
    {
        m_keys = keys;
        if (m_keys.empty())
            m_keys.resize(1);
        m_selected = std::clamp(m_selected, 0, int(m_keys.size()) - 1);
        rebuildStopEditor();
        m_bar->update();
    }

    void GradientEdit::rebuildStopEditor()
    {
        delete m_stopEditor->layout();
        // deleteLater: this can run from a click on one of these widgets
        for (QWidget* child : m_stopEditor->findChildren<QWidget*>(Qt::FindDirectChildrenOnly))
        {
            child->hide();
            child->deleteLater();
        }
        if (m_selected < 0 || m_selected >= int(m_keys.size()))
            return;
        auto* row = new QHBoxLayout(m_stopEditor);
        row->setContentsMargins(0, 0, 0, 0);
        ParticleGradientKey& key = m_keys[size_t(m_selected)];
        auto* swatch = new QPushButton;
        swatch->setFixedSize(28, 20);
        swatch->setStyleSheet(QString("background:%1; border:1px solid #222;").arg(displayColor(key.color).name()));
        swatch->setToolTip("Pick the stop's color");
        row->addWidget(swatch);
        const char* labels[4] = { "R", "G", "B", "A" };
        for (int c = 0; c < 4; ++c)
        {
            auto* s = new NumberField(key.color[c], 0.05, [this, c](double v)
                {
                    m_keys[size_t(m_selected)].color[c] = float(v);
                    m_bar->update();
                    emit keysChanged(m_keys);
                });
            s->setToolTip(labels[c]);
            row->addWidget(s);
        }
        connect(swatch, &QPushButton::clicked, this, [this]
            {
                ParticleGradientKey& k = m_keys[size_t(m_selected)];
                const float intensity = std::max({ k.color[0], k.color[1], k.color[2], 1.0f });
                const QColor picked = QColorDialog::getColor(displayColor(k.color), this, "Stop color", QColorDialog::ShowAlphaChannel);
                if (!picked.isValid())
                    return;
                k.color[0] = float(picked.redF()) * intensity;
                k.color[1] = float(picked.greenF()) * intensity;
                k.color[2] = float(picked.blueF()) * intensity;
                k.color[3] = float(picked.alphaF());
                rebuildStopEditor();
                m_bar->update();
                emit keysChanged(m_keys);
            });
    }

    // Value

    ValueEditor::ValueEditor(const ParticleParamDef& def, const ParticleValue& value, QWidget* parent)
        : QWidget(parent), m_def(def), m_value(value)
    {
        auto* layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(3);
        m_mode = new QComboBox;
        for (const char* name : kModeNames)
            m_mode->addItem(name);
        m_mode->setCurrentIndex(int(m_value.mode));
        m_mode->setToolTip("Constant, random range per particle, curve, gradient, or Graph to drive it with nodes");
        layout->addWidget(m_mode);
        m_body = new QVBoxLayout;
        m_body->setContentsMargins(0, 0, 0, 0);
        layout->addLayout(m_body);
        connect(m_mode, &QComboBox::currentIndexChanged, this, [this](int index)
            {
                const ParticleValueMode mode = ParticleValueMode(index);
                if (mode == m_value.mode)
                    return;
                if (mode == ParticleValueMode::Graph)
                {
                    emit graphRequested();
                    return; // the owner rebuilds this editor
                }
                if (mode == ParticleValueMode::Random)
                    std::copy(m_value.a, m_value.a + 4, m_value.b);
                if (mode == ParticleValueMode::Curve && m_value.curve.empty())
                {
                    if (m_value.mode != ParticleValueMode::Constant)
                        std::fill(m_value.a, m_value.a + 4, 1.0f);
                    m_value.curve = { { 0.0f, 1.0f }, { 1.0f, 1.0f } };
                }
                if (mode == ParticleValueMode::Gradient && m_value.gradient.empty())
                {
                    ParticleGradientKey k0, k1;
                    std::copy(m_value.a, m_value.a + 4, k0.color);
                    std::copy(m_value.a, m_value.a + 4, k1.color);
                    k1.time = 1.0f;
                    k1.color[3] = 0.0f;
                    m_value.gradient = { k0, k1 };
                }
                m_value.mode = mode;
                rebuild();
                emit valueChanged(m_value);
            });
        rebuild();
    }

    QWidget* ValueEditor::vectorRow(float* values, const QString& label)
    {
        auto* w = new QWidget;
        auto* row = new QHBoxLayout(w);
        row->setContentsMargins(0, 0, 0, 0);
        row->setSpacing(3);
        if (!label.isEmpty())
        {
            auto* l = new QLabel(label);
            l->setMinimumWidth(28);
            row->addWidget(l);
        }
        const int dim = std::clamp(m_def.dim, 1, 4);
        const double step = m_def.degrees ? 5.0 : 0.05;
        for (int c = 0; c < dim; ++c)
        {
            auto* s = new NumberField(values[c], step, [this, values, c](double v)
                {
                    values[c] = float(v);
                    emit valueChanged(m_value);
                });
            row->addWidget(s, 1);
        }
        if (dim == 4)
        {
            auto* swatch = new QPushButton;
            swatch->setFixedSize(24, 20);
            swatch->setStyleSheet(QString("background:%1; border:1px solid #222;").arg(displayColor(values).name()));
            row->addWidget(swatch);
            connect(swatch, &QPushButton::clicked, this, [this, values]
                {
                    const float intensity = std::max({ values[0], values[1], values[2], 1.0f });
                    const QColor picked = QColorDialog::getColor(displayColor(values), this, "Color", QColorDialog::ShowAlphaChannel);
                    if (!picked.isValid())
                        return;
                    values[0] = float(picked.redF()) * intensity;
                    values[1] = float(picked.greenF()) * intensity;
                    values[2] = float(picked.blueF()) * intensity;
                    values[3] = float(picked.alphaF());
                    rebuild();
                    emit valueChanged(m_value);
                });
        }
        return w;
    }

    void ValueEditor::rebuild()
    {
        if (m_content)
        {
            m_body->removeWidget(m_content);
            m_content->deleteLater();
        }
        m_content = new QWidget;
        auto* layout = new QVBoxLayout(m_content);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(3);
        m_body->addWidget(m_content);

        auto inputCombo = [this]()
        {
            auto* combo = new QComboBox;
            for (const char* name : kInputNames)
                combo->addItem(name);
            combo->setCurrentIndex(int(m_value.input));
            combo->setToolTip("What the curve or gradient is evaluated over");
            connect(combo, &QComboBox::currentIndexChanged, this, [this](int i)
                {
                    m_value.input = ParticleCurveInput(i);
                    emit valueChanged(m_value);
                });
            return combo;
        };

        switch (m_value.mode)
        {
        case ParticleValueMode::Constant:
            layout->addWidget(vectorRow(m_value.a, QString()));
            break;
        case ParticleValueMode::Random:
        {
            layout->addWidget(vectorRow(m_value.a, "Min"));
            layout->addWidget(vectorRow(m_value.b, "Max"));
            if (m_def.dim > 1)
            {
                auto* uniform = new QCheckBox("Uniform");
                uniform->setChecked(m_value.uniform);
                layout->addWidget(uniform);
                connect(uniform, &QCheckBox::toggled, this, [this](bool on)
                    {
                        m_value.uniform = on;
                        emit valueChanged(m_value);
                    });
            }
            break;
        }
        case ParticleValueMode::Curve:
        {
            layout->addWidget(inputCombo());
            layout->addWidget(vectorRow(m_value.a, "Scale"));
            auto* curve = new CurveEdit;
            curve->setKeys(m_value.curve);
            layout->addWidget(curve);
            connect(curve, &CurveEdit::keysChanged, this, [this](const std::vector<ParticleCurveKey>& keys)
                {
                    m_value.curve = keys;
                    emit valueChanged(m_value);
                });
            break;
        }
        case ParticleValueMode::Gradient:
        {
            layout->addWidget(inputCombo());
            auto* gradient = new GradientEdit;
            gradient->setKeys(m_value.gradient);
            layout->addWidget(gradient);
            connect(gradient, &GradientEdit::keysChanged, this, [this](const std::vector<ParticleGradientKey>& keys)
                {
                    m_value.gradient = keys;
                    emit valueChanged(m_value);
                });
            break;
        }
        case ParticleValueMode::Graph:
        {
            auto* edit = new QPushButton(QString("Open graph %1").arg(m_value.graph));
            edit->setToolTip("Shows the graph in the panel under the preview");
            layout->addWidget(edit);
            connect(edit, &QPushButton::clicked, this, [this] { emit editGraphRequested(m_value.graph); });
            break;
        }
        }
    }
}
