#include "modules/texture/TextureView.h"
#include "ui/KesStyle.h"
#include <QMouseEvent>
#include <QPainter>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>

namespace ktx
{
    TextureView::TextureView(QWidget *parent) : QWidget(parent)
    {
        setMinimumSize(200, 200);
        setMouseTracking(false);
        setFocusPolicy(Qt::ClickFocus);
    }

    void TextureView::setImage(const QImage &image, bool keepView)
    {
        m_image = image.convertToFormat(QImage::Format_RGBA8888);
        if (!m_displaySize.isValid())
            m_displaySize = m_image.size();
        rebuild();
        if (!keepView)
            fit();
        else
            update();
    }

    void TextureView::setDisplaySize(const QSize &size)
    {
        if (size == m_displaySize)
            return;
        const bool first = !m_displaySize.isValid();
        m_displaySize = size;
        if (first || m_fitted)
            fit();
        else
            update();
    }

    void TextureView::setChannels(bool r, bool g, bool b, bool a)
    {
        m_channels[0] = r;
        m_channels[1] = g;
        m_channels[2] = b;
        m_channels[3] = a;
        rebuild();
        update();
    }

    // A single colour channel shows as grey; hidden alpha shows the colour opaque.
    void TextureView::rebuild()
    {
        if (m_image.isNull())
        {
            m_display = {};
            return;
        }
        const int shown = int(m_channels[0]) + int(m_channels[1]) + int(m_channels[2]);
        int single = -1;
        if (shown == 1)
            single = m_channels[0] ? 0 : m_channels[1] ? 1 : 2;
        const bool alphaOnly = shown == 0 && m_channels[3];

        m_display = QImage(m_image.size(), QImage::Format_RGBA8888);
        for (int y = 0; y < m_image.height(); ++y)
        {
            const uchar *src = m_image.constScanLine(y);
            uchar *dst = m_display.scanLine(y);
            for (int x = 0; x < m_image.width(); ++x, src += 4, dst += 4)
            {
                if (alphaOnly)
                {
                    dst[0] = dst[1] = dst[2] = src[3];
                    dst[3] = 255;
                    continue;
                }
                if (single >= 0)
                    dst[0] = dst[1] = dst[2] = src[single];
                else
                    for (int c = 0; c < 3; ++c)
                        dst[c] = m_channels[c] ? src[c] : 0;
                dst[3] = m_channels[3] ? src[3] : 255;
            }
        }
    }

    QSizeF TextureView::shownSize() const
    {
        const QSize base = m_displaySize.isValid() ? m_displaySize : m_image.size();
        return QSizeF(base) * m_zoom;
    }

    void TextureView::fit()
    {
        const QSize base = m_displaySize.isValid() ? m_displaySize : m_image.size();
        if (base.isEmpty())
            return;
        const double margin = 24.0;
        m_zoom = std::min((width() - margin) / base.width(), (height() - margin) / base.height());
        m_zoom = std::clamp(m_zoom, 1.0 / 64.0, 64.0);
        m_center = QPointF(base.width() * 0.5, base.height() * 0.5);
        m_fitted = true;
        emit zoomChanged(m_zoom);
        update();
    }

    void TextureView::actualSize()
    {
        const QSize base = m_displaySize.isValid() ? m_displaySize : m_image.size();
        m_zoom = 1.0;
        m_center = QPointF(base.width() * 0.5, base.height() * 0.5);
        m_fitted = false;
        emit zoomChanged(m_zoom);
        update();
    }

    void TextureView::paintEvent(QPaintEvent *)
    {
        QPainter p(this);
        p.fillRect(rect(), kes::style::base());
        if (m_display.isNull())
            return;

        const QSizeF size = shownSize();
        const QPointF topLeft = QPointF(width() * 0.5, height() * 0.5) - m_center * m_zoom;
        const QRectF target(topLeft, size);

        p.save();
        p.setClipRect(target.intersected(QRectF(rect())));
        const int cell = 8;
        p.fillRect(target, QColor(58, 58, 62));
        const QColor light(82, 82, 88);
        const QRectF clip = target.intersected(QRectF(rect()));
        for (int y = int(std::floor(clip.top() / cell)); y * cell < clip.bottom(); ++y)
            for (int x = int(std::floor(clip.left() / cell)); x * cell < clip.right(); ++x)
                if ((x + y) & 1)
                    p.fillRect(QRect(x * cell, y * cell, cell, cell), light);
        p.restore();

        p.setRenderHint(QPainter::SmoothPixmapTransform, size.width() < m_display.width());
        p.drawImage(target, m_display);
        p.setPen(kes::style::border());
        p.drawRect(target.adjusted(-0.5, -0.5, 0.5, 0.5));
    }

    void TextureView::wheelEvent(QWheelEvent *event)
    {
        if (m_display.isNull())
            return;
        const double factor = std::pow(1.0015, event->angleDelta().y());
        const double zoom = std::clamp(m_zoom * factor, 1.0 / 64.0, 64.0);
        // Keep the image point under the cursor in place.
        const QPointF cursor = event->position() - QPointF(width() * 0.5, height() * 0.5);
        const QPointF under = m_center + cursor / m_zoom;
        m_zoom = zoom;
        m_center = under - cursor / m_zoom;
        m_fitted = false;
        emit zoomChanged(m_zoom);
        update();
    }

    void TextureView::mousePressEvent(QMouseEvent *event)
    {
        if (event->button() == Qt::LeftButton || event->button() == Qt::MiddleButton)
        {
            m_dragging = true;
            m_lastMouse = event->pos();
            setCursor(Qt::ClosedHandCursor);
        }
    }

    void TextureView::mouseMoveEvent(QMouseEvent *event)
    {
        if (!m_dragging)
            return;
        m_center -= QPointF(event->pos() - m_lastMouse) / m_zoom;
        m_lastMouse = event->pos();
        m_fitted = false;
        update();
    }

    void TextureView::mouseReleaseEvent(QMouseEvent *)
    {
        m_dragging = false;
        unsetCursor();
    }

    void TextureView::mouseDoubleClickEvent(QMouseEvent *)
    {
        fit();
    }

    void TextureView::resizeEvent(QResizeEvent *)
    {
        if (m_fitted)
            fit();
    }
}
