#pragma once
#include <QImage>
#include <QPointF>
#include <QWidget>

namespace ktx
{
    class TextureView : public QWidget
    {
        Q_OBJECT
    public:
        explicit TextureView(QWidget *parent = nullptr);

        // keepView: keep zoom and pan (a re-cook of the same texture), otherwise fit.
        void setImage(const QImage &image, bool keepView);
        // Shown with an image at this mip, scaled to the mip 0 size so mips line up.
        void setDisplaySize(const QSize &size);
        void setChannels(bool r, bool g, bool b, bool a);
        void fit();
        void actualSize();

    signals:
        void zoomChanged(double zoom);

    protected:
        void paintEvent(QPaintEvent *event) override;
        void wheelEvent(QWheelEvent *event) override;
        void mousePressEvent(QMouseEvent *event) override;
        void mouseMoveEvent(QMouseEvent *event) override;
        void mouseReleaseEvent(QMouseEvent *event) override;
        void mouseDoubleClickEvent(QMouseEvent *event) override;
        void resizeEvent(QResizeEvent *event) override;

    private:
        void rebuild();
        QSizeF shownSize() const;

        QImage m_image;
        QImage m_display;
        QSize m_displaySize;
        bool m_channels[4] = { true, true, true, true };
        double m_zoom = 1.0;
        QPointF m_center; // image point at the widget centre, in display-size pixels
        bool m_fitted = true;
        bool m_dragging = false;
        QPoint m_lastMouse;
    };
}
