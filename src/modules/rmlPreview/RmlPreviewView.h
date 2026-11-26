#pragma once

#include <QOpenGLWidget>
#include <QRect>
#include <QSize>
#include <QString>
#include <QTimer>

namespace Rml
{
    class Context;
    class ElementDocument;
}

namespace rmlp
{
    class RmlSink;

    // One RmlUi context drawn with OpenGL. With a fixed resolution the context has that size and
    // is scaled to fit the widget, so the layout matches the game at that resolution.
    class RmlPreviewView : public QOpenGLWidget
    {
        Q_OBJECT
    public:
        RmlPreviewView(RmlSink* sink, QWidget* parent = nullptr);
        ~RmlPreviewView() override;

        bool isReady() const { return m_context != nullptr; }
        // Empty size: the context follows the widget.
        void setResolution(const QSize& size);
        void setDensityScale(float scale);
        // Replaces the shown document. The previous one is closed even when loading fails.
        bool showDocument(const QString& absolutePath);
        void closeDocument();

    protected:
        void initializeGL() override;
        void paintGL() override;
        void resizeGL(int w, int h) override;
        void mouseMoveEvent(QMouseEvent* e) override;
        void mousePressEvent(QMouseEvent* e) override;
        void mouseReleaseEvent(QMouseEvent* e) override;
        void wheelEvent(QWheelEvent* e) override;
        void leaveEvent(QEvent* e) override;
        void keyPressEvent(QKeyEvent* e) override;
        void keyReleaseEvent(QKeyEvent* e) override;
        void showEvent(QShowEvent* e) override;
        void hideEvent(QHideEvent* e) override;

    private:
        QSize framebufferSize() const;
        QSize contextSize() const;
        // Letterboxed area of the context in framebuffer pixels, and its scale.
        QRect viewportRect(float* scale) const;
        void syncContextSize();
        bool toContext(const QPointF& widgetPos, int& x, int& y) const;
        int modifiers(Qt::KeyboardModifiers mods) const;
        void destroyVao();

        RmlSink* m_sink;
        Rml::Context* m_context = nullptr;
        Rml::ElementDocument* m_document = nullptr;
        QString m_name;
        QSize m_resolution;
        float m_density = 1.0f;
        unsigned m_vao = 0;
        QTimer m_frameTimer;
        QMetaObject::Connection m_contextConnection;
    };
}
