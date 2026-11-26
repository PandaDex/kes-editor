#include "modules/rmlPreview/RmlPreviewView.h"
#include "modules/rmlPreview/RmlRuntime.h"

#include <RmlUi/Core.h>

#include <QKeyEvent>
#include <QMouseEvent>
#include <QOpenGLContext>
#include <QOpenGLExtraFunctions>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

namespace rmlp
{
    namespace
    {
        Rml::Input::KeyIdentifier rmlKey(int key)
        {
            using namespace Rml::Input;
            if (key >= Qt::Key_A && key <= Qt::Key_Z)
                return KeyIdentifier(KI_A + (key - Qt::Key_A));
            if (key >= Qt::Key_0 && key <= Qt::Key_9)
                return KeyIdentifier(KI_0 + (key - Qt::Key_0));
            if (key >= Qt::Key_F1 && key <= Qt::Key_F12)
                return KeyIdentifier(KI_F1 + (key - Qt::Key_F1));
            switch (key)
            {
            case Qt::Key_Space: return KI_SPACE;
            case Qt::Key_Return: return KI_RETURN;
            case Qt::Key_Enter: return KI_NUMPADENTER;
            case Qt::Key_Escape: return KI_ESCAPE;
            case Qt::Key_Backspace: return KI_BACK;
            case Qt::Key_Tab:
            case Qt::Key_Backtab: return KI_TAB;
            case Qt::Key_Delete: return KI_DELETE;
            case Qt::Key_Insert: return KI_INSERT;
            case Qt::Key_Home: return KI_HOME;
            case Qt::Key_End: return KI_END;
            case Qt::Key_PageUp: return KI_PRIOR;
            case Qt::Key_PageDown: return KI_NEXT;
            case Qt::Key_Left: return KI_LEFT;
            case Qt::Key_Right: return KI_RIGHT;
            case Qt::Key_Up: return KI_UP;
            case Qt::Key_Down: return KI_DOWN;
            case Qt::Key_Shift: return KI_LSHIFT;
            case Qt::Key_Control: return KI_LCONTROL;
            case Qt::Key_Alt: return KI_LMENU;
            default: return KI_UNKNOWN;
            }
        }

        int rmlButton(Qt::MouseButton button)
        {
            switch (button)
            {
            case Qt::LeftButton: return 0;
            case Qt::RightButton: return 1;
            case Qt::MiddleButton: return 2;
            default: return -1;
            }
        }

        int gNextContext = 0;
    }

    RmlPreviewView::RmlPreviewView(RmlSink* sink, QWidget* parent)
        : QOpenGLWidget(parent)
        , m_sink(sink)
        , m_name(QStringLiteral("forge_preview_%1").arg(++gNextContext))
    {
        setMouseTracking(true);
        setFocusPolicy(Qt::StrongFocus);
        setMinimumSize(160, 90);
        if (runtime::acquire())
        {
            SinkScope scope(m_sink);
            m_context = Rml::CreateContext(m_name.toStdString(), { 1280, 720 });
            if (m_context)
                runtime::registerContext(m_context, m_sink);
        }
        if (!m_context)
            runtime::release();
        m_frameTimer.setInterval(16);
        connect(&m_frameTimer, &QTimer::timeout, this, qOverload<>(&QWidget::update));
    }

    RmlPreviewView::~RmlPreviewView()
    {
        m_frameTimer.stop();
        if (!m_context)
            return;
        makeCurrent();
        {
            SinkScope scope(m_sink);
            runtime::unregisterContext(m_context);
            Rml::RemoveContext(m_name.toStdString());
            m_context = nullptr;
            m_document = nullptr;
        }
        destroyVao();
        runtime::release();
        doneCurrent();
    }

    void RmlPreviewView::setResolution(const QSize& size)
    {
        m_resolution = size;
        syncContextSize();
        update();
    }

    void RmlPreviewView::setDensityScale(float scale)
    {
        m_density = scale > 0.0f ? scale : 1.0f;
        syncContextSize();
        update();
    }

    bool RmlPreviewView::showDocument(const QString& absolutePath)
    {
        if (!m_context)
            return false;
        makeCurrent();
        runtime::ensureCurrentContext();
        SinkScope scope(m_sink);
        if (m_document)
        {
            m_document->Close();
            m_context->Update(); // documents are destroyed on the next update
            m_document = nullptr;
        }
        Rml::Factory::ClearStyleSheetCache();
        Rml::Factory::ClearTemplateCache();
        Rml::ReleaseTextures();
        syncContextSize();
        m_document = m_context->LoadDocument(QString(absolutePath).replace('\\', '/').toStdString());
        if (m_document)
            m_document->Show(Rml::ModalFlag::None, Rml::FocusFlag::None);
        m_context->Update();
        update();
        return m_document != nullptr;
    }

    void RmlPreviewView::closeDocument()
    {
        if (!m_context || !m_document)
            return;
        makeCurrent();
        runtime::ensureCurrentContext();
        SinkScope scope(m_sink);
        m_document->Close();
        m_context->Update();
        m_document = nullptr;
        update();
    }

    void RmlPreviewView::initializeGL()
    {
        // A new GL context (after a reparent) needs a new VAO; buffers and textures are shared.
        m_vao = 0;
        QOpenGLExtraFunctions* f = context()->extraFunctions();
        GLuint vao = 0;
        f->glGenVertexArrays(1, &vao);
        m_vao = vao;
        disconnect(m_contextConnection);
        m_contextConnection = connect(context(), &QOpenGLContext::aboutToBeDestroyed, this, [this]
            {
            makeCurrent();
            destroyVao();
            doneCurrent(); });
    }

    void RmlPreviewView::destroyVao()
    {
        if (!m_vao || !QOpenGLContext::currentContext())
            return;
        GLuint vao = m_vao;
        QOpenGLContext::currentContext()->extraFunctions()->glDeleteVertexArrays(1, &vao);
        m_vao = 0;
    }

    void RmlPreviewView::resizeGL(int, int)
    {
        syncContextSize();
    }

    QSize RmlPreviewView::framebufferSize() const
    {
        const qreal dpr = devicePixelRatioF();
        return { std::max(1, int(std::lround(width() * dpr))), std::max(1, int(std::lround(height() * dpr))) };
    }

    QSize RmlPreviewView::contextSize() const
    {
        return m_resolution.isEmpty() ? framebufferSize() : m_resolution;
    }

    QRect RmlPreviewView::viewportRect(float* scale) const
    {
        const QSize fb = framebufferSize();
        const QSize cs = contextSize();
        const float s = std::min(float(fb.width()) / cs.width(), float(fb.height()) / cs.height());
        const int w = int(std::lround(cs.width() * s));
        const int h = int(std::lround(cs.height() * s));
        if (scale)
            *scale = s;
        return { (fb.width() - w) / 2, (fb.height() - h) / 2, w, h };
    }

    void RmlPreviewView::syncContextSize()
    {
        if (!m_context)
            return;
        const QSize cs = contextSize();
        SinkScope scope(m_sink);
        m_context->SetDimensions({ cs.width(), cs.height() });
        // A fixed resolution previews a 1x display; Fit follows this monitor like the game does.
        const float display = m_resolution.isEmpty() ? float(devicePixelRatioF()) : 1.0f;
        m_context->SetDensityIndependentPixelRatio(display * m_density);
    }

    void RmlPreviewView::paintGL()
    {
        QOpenGLExtraFunctions* f = context()->extraFunctions();
        const QSize fb = framebufferSize();
        f->glViewport(0, 0, fb.width(), fb.height());
        f->glDisable(GL_SCISSOR_TEST);
        f->glClearColor(0.043f, 0.051f, 0.071f, 1.0f);
        f->glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        if (!m_context || !m_vao)
            return;

        float scale = 1.0f;
        const QRect vp = viewportRect(&scale);
        f->glEnable(GL_SCISSOR_TEST);
        f->glScissor(vp.x(), fb.height() - vp.bottom() - 1, vp.width(), vp.height());
        f->glClearColor(0.086f, 0.102f, 0.133f, 1.0f);
        f->glClear(GL_COLOR_BUFFER_BIT);
        f->glDisable(GL_SCISSOR_TEST);

        // The projection maps the context to the letterbox; glViewport does the placement.
        f->glViewport(vp.x(), fb.height() - vp.bottom() - 1, vp.width(), vp.height());
        SinkScope scope(m_sink);
        m_context->Update();
        const QSize cs = contextSize();
        runtime::beginFrame(m_vao, fb.height(), vp.x(), vp.y(), scale, cs.width(), cs.height());
        m_context->Render();
        runtime::endFrame();
    }

    bool RmlPreviewView::toContext(const QPointF& widgetPos, int& x, int& y) const
    {
        float scale = 1.0f;
        const QRect vp = viewportRect(&scale);
        const qreal dpr = devicePixelRatioF();
        x = int(std::floor((widgetPos.x() * dpr - vp.x()) / scale));
        y = int(std::floor((widgetPos.y() * dpr - vp.y()) / scale));
        const QSize cs = contextSize();
        return x >= 0 && y >= 0 && x < cs.width() && y < cs.height();
    }

    int RmlPreviewView::modifiers(Qt::KeyboardModifiers mods) const
    {
        int out = 0;
        if (mods & Qt::ControlModifier)
            out |= Rml::Input::KM_CTRL;
        if (mods & Qt::ShiftModifier)
            out |= Rml::Input::KM_SHIFT;
        if (mods & Qt::AltModifier)
            out |= Rml::Input::KM_ALT;
        return out;
    }

    void RmlPreviewView::mouseMoveEvent(QMouseEvent* e)
    {
        int x = 0, y = 0;
        if (!m_context)
            return;
        SinkScope scope(m_sink);
        if (toContext(e->position(), x, y) || e->buttons())
            m_context->ProcessMouseMove(x, y, modifiers(e->modifiers()));
        else
            m_context->ProcessMouseLeave();
    }

    void RmlPreviewView::mousePressEvent(QMouseEvent* e)
    {
        setFocus();
        const int button = rmlButton(e->button());
        int x = 0, y = 0;
        if (!m_context || button < 0 || !toContext(e->position(), x, y))
            return;
        SinkScope scope(m_sink);
        m_context->ProcessMouseMove(x, y, modifiers(e->modifiers()));
        m_context->ProcessMouseButtonDown(button, modifiers(e->modifiers()));
    }

    void RmlPreviewView::mouseReleaseEvent(QMouseEvent* e)
    {
        const int button = rmlButton(e->button());
        if (!m_context || button < 0)
            return;
        SinkScope scope(m_sink);
        m_context->ProcessMouseButtonUp(button, modifiers(e->modifiers()));
    }

    void RmlPreviewView::wheelEvent(QWheelEvent* e)
    {
        if (!m_context)
            return;
        SinkScope scope(m_sink);
        const QPoint delta = e->angleDelta();
        m_context->ProcessMouseWheel(Rml::Vector2f(-delta.x() / 120.0f, -delta.y() / 120.0f), modifiers(e->modifiers()));
    }

    void RmlPreviewView::leaveEvent(QEvent* e)
    {
        QOpenGLWidget::leaveEvent(e);
        if (!m_context)
            return;
        SinkScope scope(m_sink);
        m_context->ProcessMouseLeave();
    }

    void RmlPreviewView::keyPressEvent(QKeyEvent* e)
    {
        if (!m_context)
            return;
        SinkScope scope(m_sink);
        const int mods = modifiers(e->modifiers());
        const Rml::Input::KeyIdentifier key = rmlKey(e->key());
        if (key != Rml::Input::KI_UNKNOWN)
            m_context->ProcessKeyDown(key, mods);
        const QString text = e->text();
        if (!text.isEmpty() && text.at(0).isPrint() && !(e->modifiers() & Qt::ControlModifier))
            m_context->ProcessTextInput(text.toStdString());
    }

    void RmlPreviewView::keyReleaseEvent(QKeyEvent* e)
    {
        if (!m_context)
            return;
        SinkScope scope(m_sink);
        const Rml::Input::KeyIdentifier key = rmlKey(e->key());
        if (key != Rml::Input::KI_UNKNOWN)
            m_context->ProcessKeyUp(key, modifiers(e->modifiers()));
    }

    void RmlPreviewView::showEvent(QShowEvent* e)
    {
        QOpenGLWidget::showEvent(e);
        m_frameTimer.start();
    }

    void RmlPreviewView::hideEvent(QHideEvent* e)
    {
        QOpenGLWidget::hideEvent(e);
        m_frameTimer.stop();
    }
}
