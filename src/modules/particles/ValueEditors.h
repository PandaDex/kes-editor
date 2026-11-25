#pragma once

#include "particle_asset.h"

#include <QWidget>
#include <vector>

class QComboBox;
class QVBoxLayout;

namespace kpe
{
    // Keys over x (0..1 unless keys go further); drag to move, double-click to add,
    // right-click to remove.
    class CurveEdit : public QWidget
    {
        Q_OBJECT
    public:
        explicit CurveEdit(QWidget* parent = nullptr);
        void setKeys(const std::vector<ParticleCurveKey>& keys);
        QSize sizeHint() const override { return { 240, 110 }; }

    signals:
        void keysChanged(const std::vector<ParticleCurveKey>& keys);

    protected:
        void paintEvent(QPaintEvent*) override;
        void mousePressEvent(QMouseEvent*) override;
        void mouseMoveEvent(QMouseEvent*) override;
        void mouseReleaseEvent(QMouseEvent*) override;
        void mouseDoubleClickEvent(QMouseEvent*) override;

    private:
        QRectF plotRect() const;
        void range(float& x1, float& y0, float& y1) const;
        QPointF toScreen(const ParticleCurveKey& k) const;
        ParticleCurveKey fromScreen(const QPointF& p) const;
        int hit(const QPointF& p) const;

        std::vector<ParticleCurveKey> m_keys;
        int m_drag = -1;
        float m_frozenRange[3] = {};
    };

    // Color stops; drag to move, double-click the bar to add, right-click a stop to remove.
    // The selected stop is edited below the bar (HDR intensity and alpha included).
    class GradientEdit : public QWidget
    {
        Q_OBJECT
    public:
        explicit GradientEdit(QWidget* parent = nullptr);
        void setKeys(const std::vector<ParticleGradientKey>& keys);

    signals:
        void keysChanged(const std::vector<ParticleGradientKey>& keys);

    private:
        class Bar;
        void rebuildStopEditor();

        std::vector<ParticleGradientKey> m_keys;
        int m_selected = 0;
        Bar* m_bar = nullptr;
        QWidget* m_stopEditor = nullptr;
    };

    // One module or renderer parameter: a mode (constant, random, curve, gradient,
    // graph) and the editor for it.
    class ValueEditor : public QWidget
    {
        Q_OBJECT
    public:
        ValueEditor(const ParticleParamDef& def, const ParticleValue& value, QWidget* parent = nullptr);

    signals:
        void valueChanged(const ParticleValue& value);
        // The user picked Graph: the owner creates (or reuses) a graph and sets the value.
        void graphRequested();
        void editGraphRequested(int graphId);

    private:
        void rebuild();
        QWidget* vectorRow(float* values, const QString& label);

        ParticleParamDef m_def;
        ParticleValue m_value;
        QComboBox* m_mode = nullptr;
        QVBoxLayout* m_body = nullptr;
        QWidget* m_content = nullptr;
    };
}
