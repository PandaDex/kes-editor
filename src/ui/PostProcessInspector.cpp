#include "ui/PostProcessInspector.h"
#include "core/MapObject.h"
#include "ui/KesStyle.h"
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHash>
#include <QImage>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QSharedPointer>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

namespace
{
    QString formatNumber(double v)
    {
        const QString s = QString::number(std::round(v * 10000.0) / 10000.0, 'g', 7);
        return s == "-0" ? QString("0") : s;
    }

    bool parseNumber(const QString &text, double &out)
    {
        bool ok = false;
        const double v = QLocale::c().toDouble(text.trimmed().replace(',', '.'), &ok);
        if (ok)
            out = v;
        return ok;
    }

    QVector3D parseVector(const QString &text, const QVector3D &fallback)
    {
        const QStringList parts = text.simplified().split(' ', Qt::SkipEmptyParts);
        if (parts.size() != 3)
            return fallback;
        QVector3D v;
        for (int i = 0; i < 3; ++i)
        {
            double d = 0.0;
            if (!parseNumber(parts[i], d))
                return fallback;
            v[i] = float(d);
        }
        return v;
    }

    QString formatVector(const QVector3D &v)
    {
        return QString("%1 %2 %3").arg(formatNumber(v.x()), formatNumber(v.y()), formatNumber(v.z()));
    }

    // Chroma plane: two orthonormal directions perpendicular to grey (1,1,1).
    const QVector3D kChromaX = QVector3D(2.f, -1.f, -1.f) / std::sqrt(6.f);
    const QVector3D kChromaY = QVector3D(0.f, 1.f, -1.f) / std::sqrt(2.f);

    enum class Kind
    {
        Number,
        Toggle, // "1" / "0"
        Choice,
        Color, // 0-1 RGB vector
        WheelMultiply,
        WheelOffset,
    };

    struct Setting
    {
        const char *key;
        const char *label;
        Kind kind;
        const char *engineDefault; // written when the override is switched on
        double softMin = 0.0, softMax = 1.0, hardMin = 0.0, hardMax = 1.0;
        int decimals = 2;
        QVector<QPair<QString, QString>> choices = {}; // value, label
    };

    struct Section
    {
        const char *title;
        QVector<Setting> settings;
    };

    const QVector<Section> &sections()
    {
        static const QVector<Section> list = {
            {"Exposure",
             {{"AutoExposure", "Auto Exposure", Kind::Toggle, "1"},
              {"ExposureCompensation", "Compensation (EV)", Kind::Number, "0", -5, 5, -16, 16, 2},
              {"AutoExposureMin", "Min EV", Kind::Number, "-5", -10, 10, -16, 16, 2},
              {"AutoExposureMax", "Max EV", Kind::Number, "3", -10, 10, -16, 16, 2},
              {"AdaptationSpeedUp", "Speed Up", Kind::Number, "3", 0, 10, 0, 100, 2},
              {"AdaptationSpeedDown", "Speed Down", Kind::Number, "1", 0, 10, 0, 100, 2}}},
            {"Bloom",
             {{"BloomIntensity", "Intensity", Kind::Number, "0.05", 0, 0.5, 0, 1, 3},
              {"BloomRadius", "Radius", Kind::Number, "1", 0, 4, 0, 4, 2}}},
            {"Tonemapper",
             {{"Tonemapper", "Curve", Kind::Choice, "0", 0, 0, 0, 0, 0,
               {{"0", "ACES"}, {"1", "AgX"}, {"2", "Neutral"}, {"3", "Reinhard"}, {"4", "None"}}}}},
            {"White Balance",
             {{"Temperature", "Temperature", Kind::Number, "0", -1, 1, -1, 1, 2},
              {"Tint", "Tint", Kind::Number, "0", -1, 1, -1, 1, 2}}},
            {"Color Grading",
             {{"Saturation", "Saturation", Kind::Number, "1", 0, 2, 0, 4, 2},
              {"Contrast", "Contrast", Kind::Number, "1", 0, 2, 0, 4, 2},
              {"ColorFilter", "Color Filter", Kind::Color, "1 1 1"},
              {"Lift", "Lift", Kind::WheelOffset, "0 0 0"},
              {"Gamma", "Gamma", Kind::WheelMultiply, "1 1 1"},
              {"Gain", "Gain", Kind::WheelMultiply, "1 1 1"}}},
            {"Vignette",
             {{"VignetteIntensity", "Intensity", Kind::Number, "0", 0, 1, 0, 1, 2},
              {"VignetteSmoothness", "Smoothness", Kind::Number, "0.5", 0, 1, 0, 1, 2},
              {"VignetteColor", "Color", Kind::Color, "0 0 0"}}},
            {"Sharpen",
             {{"Sharpen", "Amount", Kind::Number, "0", 0, 1, 0, 1, 2}}},
            {"Ambient Occlusion",
             {{"AmbientOcclusionIntensity", "Intensity", Kind::Number, "1", 0, 4, 0, 8, 2},
              {"AmbientOcclusionRadius", "Radius (m)", Kind::Number, "0.5", 0.05, 3, 0.01, 10, 2}}},
            {"Contact Shadows",
             {{"ContactShadowLength", "Length (m)", Kind::Number, "0.3", 0, 2, 0, 5, 2}}},
            {"Reflections",
             {{"ReflectionIntensity", "Intensity", Kind::Number, "1", 0, 1, 0, 1, 2},
              {"ReflectionMaxRoughness", "Max Roughness", Kind::Number, "0.5", 0, 1, 0, 1, 2},
              {"ReflectionMaxDistance", "Max Distance (m)", Kind::Number, "30", 1, 100, 1, 200, 1}}},
            {"Depth of Field",
             {{"DepthOfFieldMaxBlur", "Max Blur (%)", Kind::Number, "0", 0, 5, 0, 5, 2},
              {"DepthOfFieldFocusDistance", "Focus Distance (m)", Kind::Number, "10", 0, 100, 0, 10000, 2},
              {"DepthOfFieldFocusRange", "Focus Range (m)", Kind::Number, "10", 0.1, 100, 0.01, 10000, 2}}},
            {"Motion Blur",
             {{"MotionBlurIntensity", "Intensity", Kind::Number, "0", 0, 2, 0, 2, 2}}},
        };
        return list;
    }

    // Collapsed state survives rebuilds (selection changes) for the session.
    QHash<QString, bool> &expandedSections()
    {
        static QHash<QString, bool> state;
        return state;
    }

    QColor toColor(const QVector3D &v)
    {
        auto c = [](float f) { return int(std::clamp(f, 0.f, 1.f) * 255.f + 0.5f); };
        return QColor(c(v.x()), c(v.y()), c(v.z()));
    }
} // namespace

bool isPostProcessVolumeClass(const QString &classname)
{
    return classname == QLatin1String("PostProcessVolumeEntity");
}

bool isPostProcessInspectorKey(const QString &key)
{
    for (const Section &s : sections())
        for (const Setting &st : s.settings)
            if (key == QLatin1String(st.key))
                return true;
    return false;
}

// ValueBar

ValueBar::ValueBar(double softMin, double softMax, double hardMin, double hardMax, int decimals, QWidget *parent)
    : QWidget(parent), m_softMin(softMin), m_softMax(softMax), m_hardMin(hardMin), m_hardMax(hardMax), m_decimals(decimals)
{
    setFocusPolicy(Qt::StrongFocus);
    setCursor(Qt::SizeHorCursor);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

QSize ValueBar::sizeHint() const
{
    return {120, 22};
}

QSize ValueBar::minimumSizeHint() const
{
    return {50, 22};
}

void ValueBar::setValue(double v)
{
    m_value = v;
    update();
}

double ValueBar::fromX(double x) const
{
    const double t = std::clamp(x / std::max(1, width()), 0.0, 1.0);
    return m_softMin + t * (m_softMax - m_softMin);
}

void ValueBar::edit(double v, bool final)
{
    const double step = std::pow(10.0, -m_decimals);
    v = std::round(std::clamp(v, m_hardMin, m_hardMax) / step) * step;
    m_value = v;
    update();
    emit valueEdited(v, final);
}

void ValueBar::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    const bool enabled = isEnabled();
    p.setPen(QPen(hasFocus() ? kes::style::accent() : kes::style::border(), 1.0));
    p.setBrush(kes::style::base());
    p.drawRoundedRect(r, 3, 3);

    const double span = m_softMax - m_softMin;
    const double t = span > 0.0 ? std::clamp((m_value - m_softMin) / span, 0.0, 1.0) : 0.0;
    // Fill from zero when the range crosses it, so signed values read as signed.
    const double zero = (m_softMin < 0.0 && m_softMax > 0.0) ? -m_softMin / span : 0.0;
    QRectF fill = r.adjusted(1, 1, -1, -1);
    const double x0 = fill.left() + fill.width() * std::min(zero, t);
    const double x1 = fill.left() + fill.width() * std::max(zero, t);
    fill.setLeft(x0);
    fill.setRight(std::max(x1, x0 + 1.0));
    QColor accent = enabled ? kes::style::accentDim() : kes::style::border();
    accent.setAlpha(enabled ? 170 : 120);
    p.setPen(Qt::NoPen);
    p.setBrush(accent);
    p.drawRoundedRect(fill, 2, 2);

    p.setPen(enabled ? kes::style::text() : kes::style::disabled());
    p.drawText(r, Qt::AlignCenter, QString::number(m_value, 'f', m_decimals));
}

void ValueBar::mousePressEvent(QMouseEvent *e)
{
    if (e->button() != Qt::LeftButton)
        return;
    setFocus(Qt::MouseFocusReason);
    m_dragging = true;
    m_moved = false;
    m_pressX = e->position().x();
    m_pressValue = m_value;
}

void ValueBar::mouseMoveEvent(QMouseEvent *e)
{
    if (!m_dragging)
        return;
    const double dx = e->position().x() - m_pressX;
    if (!m_moved && std::abs(dx) < 3.0)
        return;
    m_moved = true;
    // Relative scrub over the bar width; Shift for fine steps.
    const double scale = (e->modifiers() & Qt::ShiftModifier) ? 0.1 : 1.0;
    edit(m_pressValue + dx / std::max(1, width()) * (m_softMax - m_softMin) * scale, false);
}

void ValueBar::mouseReleaseEvent(QMouseEvent *e)
{
    if (!m_dragging || e->button() != Qt::LeftButton)
        return;
    m_dragging = false;
    if (m_moved)
        edit(m_value, true);
    else
        edit(fromX(e->position().x()), true); // click jumps to the position
}

void ValueBar::mouseDoubleClickEvent(QMouseEvent *)
{
    m_dragging = false;
    startTyping();
}

void ValueBar::keyPressEvent(QKeyEvent *e)
{
    const double step = (m_softMax - m_softMin) / ((e->modifiers() & Qt::ShiftModifier) ? 100.0 : 20.0);
    if (e->key() == Qt::Key_Left || e->key() == Qt::Key_Down)
        edit(m_value - step, true);
    else if (e->key() == Qt::Key_Right || e->key() == Qt::Key_Up)
        edit(m_value + step, true);
    else if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter || e->key() == Qt::Key_F2)
        startTyping();
    else
        QWidget::keyPressEvent(e);
}

void ValueBar::startTyping()
{
    auto *line = new QLineEdit(QString::number(m_value, 'g', 7), this);
    line->setGeometry(rect());
    line->setAlignment(Qt::AlignCenter);
    line->selectAll();
    line->show();
    line->setFocus();
    auto done = QSharedPointer<bool>::create(false);
    connect(line, &QLineEdit::editingFinished, this, [this, line, done]
            {
        if (*done)
            return;
        *done = true;
        double v = 0.0;
        if (parseNumber(line->text(), v))
            edit(v, true);
        line->deleteLater(); });
}

// ColorWheel

class ColorWheel::Disc : public QWidget
{
public:
    std::function<void(float, float, bool)> onMove;
    float x = 0.f, y = 0.f;

    explicit Disc(QWidget *parent) : QWidget(parent)
    {
        setFixedSize(112, 112);
        setCursor(Qt::CrossCursor);
        setToolTip(QObject::tr("Drag to tint, double-click to reset."));
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const QImage &img = wheelImage();
        p.drawImage(rect().topLeft(), img);
        const QRectF disc = QRectF(rect()).adjusted(1.5, 1.5, -1.5, -1.5);
        p.setPen(QPen(kes::style::border(), 1.0));
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(disc);
        // crosshair at neutral
        p.setPen(QPen(QColor(0, 0, 0, 60), 1.0));
        p.drawLine(QPointF(disc.center().x() - 4, disc.center().y()), QPointF(disc.center().x() + 4, disc.center().y()));
        p.drawLine(QPointF(disc.center().x(), disc.center().y() - 4), QPointF(disc.center().x(), disc.center().y() + 4));

        const QPointF marker = toWidget(x, y);
        p.setPen(QPen(Qt::black, 2.0));
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(marker, 5.0, 5.0);
        p.setPen(QPen(isEnabled() ? QColor(Qt::white) : kes::style::disabled(), 1.5));
        p.drawEllipse(marker, 5.0, 5.0);
    }

    void mousePressEvent(QMouseEvent *e) override
    {
        if (e->button() == Qt::LeftButton)
            moveTo(e->position(), false);
    }
    void mouseMoveEvent(QMouseEvent *e) override
    {
        if (e->buttons() & Qt::LeftButton)
            moveTo(e->position(), false);
    }
    void mouseReleaseEvent(QMouseEvent *e) override
    {
        if (e->button() == Qt::LeftButton)
            moveTo(e->position(), true);
    }
    void mouseDoubleClickEvent(QMouseEvent *) override
    {
        x = y = 0.f;
        update();
        if (onMove)
            onMove(0.f, 0.f, true);
    }

private:
    float radius() const { return width() * 0.5f - 2.f; }

    QPointF toWidget(float wx, float wy) const
    {
        return {width() * 0.5 + wx * radius(), height() * 0.5 - wy * radius()};
    }

    void moveTo(const QPointF &pos, bool final)
    {
        float nx = float((pos.x() - width() * 0.5) / radius());
        float ny = float((height() * 0.5 - pos.y()) / radius());
        const float len = std::sqrt(nx * nx + ny * ny);
        if (len > 1.f)
        {
            nx /= len;
            ny /= len;
        }
        x = nx;
        y = ny;
        update();
        if (onMove)
            onMove(x, y, final);
    }

    // Each point shows the tint its position applies (same chroma basis as the mapping).
    const QImage &wheelImage() const
    {
        static QImage image;
        if (image.size() != size())
        {
            image = QImage(size(), QImage::Format_ARGB32_Premultiplied);
            image.fill(Qt::transparent);
            const float r = radius();
            for (int py = 0; py < height(); ++py)
            {
                for (int px = 0; px < width(); ++px)
                {
                    const float nx = (px + 0.5f - width() * 0.5f) / r;
                    const float ny = (height() * 0.5f - (py + 0.5f)) / r;
                    const float d = std::sqrt(nx * nx + ny * ny);
                    if (d > 1.02f)
                        continue;
                    const QVector3D chroma = (kChromaX * nx + kChromaY * ny) * 0.75f;
                    const QVector3D c = QVector3D(0.5f, 0.5f, 0.5f) + chroma;
                    const float alpha = std::clamp((1.02f - d) / 0.02f, 0.f, 1.f);
                    QColor col = toColor(c);
                    image.setPixel(px, py, qPremultiply(qRgba(col.red(), col.green(), col.blue(), int(alpha * 255))));
                }
            }
        }
        return image;
    }
};

ColorWheel::ColorWheel(Mode mode, QWidget *parent) : QWidget(parent), m_mode(mode)
{
    auto *v = new QVBoxLayout(this);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(3);
    m_disc = new Disc(this);
    v->addWidget(m_disc, 0, Qt::AlignHCenter);
    if (mode == Mode::Multiply)
        m_levelBar = new ValueBar(0.0, 2.0, 0.01, 4.0, 2, this);
    else
        m_levelBar = new ValueBar(-0.5, 0.5, -1.0, 1.0, 2, this);
    m_levelBar->setToolTip(mode == Mode::Multiply ? tr("Level (1 = unchanged)") : tr("Offset (0 = unchanged)"));
    v->addWidget(m_levelBar);
    m_readout = new QLabel(this);
    m_readout->setAlignment(Qt::AlignCenter);
    QFont f = m_readout->font();
    f.setPointSizeF(f.pointSizeF() * 0.85);
    m_readout->setFont(f);
    v->addWidget(m_readout);

    m_disc->onMove = [this](float x, float y, bool final)
    {
        m_x = x;
        m_y = y;
        toValue(final);
    };
    connect(m_levelBar, &ValueBar::valueEdited, this, [this](double level, bool final)
            {
        m_level = float(level);
        toValue(final); });
    setValue(mode == Mode::Multiply ? QVector3D(1, 1, 1) : QVector3D(0, 0, 0));
}

// Multiply: v = level * (1 + k * chroma). Offset: v = level + k * chroma.
static constexpr float kMultiplyRange = 0.6f;
static constexpr float kOffsetRange = 0.3f;

void ColorWheel::setValue(const QVector3D &v)
{
    m_value = v;
    fromValue();
}

void ColorWheel::fromValue()
{
    const QVector3D v = m_value;
    const float mean = (v.x() + v.y() + v.z()) / 3.f;
    QVector3D chroma;
    if (m_mode == Mode::Multiply)
    {
        m_level = mean;
        chroma = std::abs(mean) > 1e-6f ? (v / mean - QVector3D(1, 1, 1)) / kMultiplyRange : QVector3D();
    }
    else
    {
        m_level = mean;
        chroma = (v - QVector3D(mean, mean, mean)) / kOffsetRange;
    }
    m_x = QVector3D::dotProduct(chroma, kChromaX);
    m_y = QVector3D::dotProduct(chroma, kChromaY);
    const float len = std::sqrt(m_x * m_x + m_y * m_y);
    if (len > 1.f)
    {
        m_x /= len;
        m_y /= len;
    }
    m_disc->x = m_x;
    m_disc->y = m_y;
    m_disc->update();
    m_levelBar->setValue(m_level);
    m_readout->setText(QString("R %1  G %2  B %3").arg(v.x(), 0, 'f', 2).arg(v.y(), 0, 'f', 2).arg(v.z(), 0, 'f', 2));
}

void ColorWheel::toValue(bool final)
{
    const QVector3D chroma = kChromaX * m_x + kChromaY * m_y;
    QVector3D v;
    if (m_mode == Mode::Multiply)
        v = m_level * (QVector3D(1, 1, 1) + chroma * kMultiplyRange);
    else
        v = QVector3D(m_level, m_level, m_level) + chroma * kOffsetRange;
    for (int i = 0; i < 3; ++i)
        v[i] = std::round(v[i] * 1000.f) / 1000.f;
    m_value = v;
    m_readout->setText(QString("R %1  G %2  B %3").arg(v.x(), 0, 'f', 2).arg(v.y(), 0, 'f', 2).arg(v.z(), 0, 'f', 2));
    emit valueEdited(v, final);
}

// Inspector

namespace
{
    // One setting: override toggle, label, editor. Editing a value that isn't
    // overridden yet switches the override on.
    struct Row
    {
        QCheckBox *toggle = nullptr;
        QLabel *label = nullptr;
        std::function<void(const QString &)> show; // displays a stored or default value
    };

    QWidget *makeSectionHeader(const QString &title, QToolButton **arrow, QCheckBox **toggle, QLabel **count)
    {
        auto *header = new QWidget;
        header->setObjectName("PostProcessSectionHeader");
        header->setAttribute(Qt::WA_StyledBackground, true);
        header->setStyleSheet(QString("#PostProcessSectionHeader { background: %1; border: 1px solid %2; border-radius: 3px; }")
                                  .arg(kes::style::panel().name(), kes::style::border().name()));
        auto *h = new QHBoxLayout(header);
        h->setContentsMargins(4, 2, 6, 2);
        h->setSpacing(6);
        *arrow = new QToolButton;
        (*arrow)->setAutoRaise(true);
        (*arrow)->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        (*arrow)->setText(title);
        QFont bold = (*arrow)->font();
        bold.setBold(true);
        (*arrow)->setFont(bold);
        h->addWidget(*arrow);
        h->addStretch();
        *count = new QLabel;
        (*count)->setStyleSheet(QString("color: %1;").arg(kes::style::muted().name()));
        h->addWidget(*count);
        *toggle = new QCheckBox;
        (*toggle)->setTristate(true);
        (*toggle)->setToolTip(QObject::tr("Override every setting in this group"));
        h->addWidget(*toggle);
        return header;
    }
} // namespace

QWidget *buildPostProcessInspector(const MapObject &o, const PostProcessInspectorHost &host)
{
    auto *root = new QWidget;
    auto *lay = new QVBoxLayout(root);
    lay->setContentsMargins(0, 6, 0, 0);
    lay->setSpacing(4);

    for (const Section &section : sections())
    {
        QToolButton *arrow = nullptr;
        QCheckBox *sectionToggle = nullptr;
        QLabel *count = nullptr;
        QWidget *header = makeSectionHeader(QObject::tr(section.title), &arrow, &sectionToggle, &count);
        lay->addWidget(header);

        auto *body = new QWidget;
        auto *grid = new QGridLayout(body);
        grid->setContentsMargins(6, 2, 2, 6);
        grid->setHorizontalSpacing(6);
        grid->setVerticalSpacing(4);
        grid->setColumnStretch(2, 1);
        lay->addWidget(body);

        auto rows = QSharedPointer<QVector<Row>>::create();
        const QStringList keys = [&]
        {
            QStringList k;
            for (const Setting &s : section.settings)
                k << s.key;
            return k;
        }();

        // Header state from the stored values.
        auto refreshHeader = [rows, sectionToggle, count]
        {
            int on = 0;
            for (const Row &r : *rows)
                on += r.toggle->isChecked() ? 1 : 0;
            QSignalBlocker b(sectionToggle);
            sectionToggle->setCheckState(on == 0 ? Qt::Unchecked : on == rows->size() ? Qt::Checked : Qt::PartiallyChecked);
            count->setText(on ? QString("%1/%2").arg(on).arg(rows->size()) : QString());
        };

        int gridRow = 0;
        for (const Setting &st : section.settings)
        {
            const QString key = st.key;
            const QString engineDefault = st.engineDefault;
            const QString stored = o.kv.value(key);
            Row row;
            row.toggle = new QCheckBox;
            row.toggle->setToolTip(QObject::tr("Override %1").arg(key));
            row.label = new QLabel(QObject::tr(st.label));
            row.label->setToolTip(key);

            QWidget *editor = nullptr;
            // Writes a value; the override switches on with it.
            auto write = [host, key](const QString &value) { host.set(key, value); };

            switch (st.kind)
            {
            case Kind::Number:
            {
                auto *bar = new ValueBar(st.softMin, st.softMax, st.hardMin, st.hardMax, st.decimals);
                QObject::connect(bar, &ValueBar::valueEdited, bar, [write](double v, bool) { write(formatNumber(v)); });
                row.show = [bar, engineDefault](const QString &v)
                {
                    double d = 0.0;
                    if (!parseNumber(v.isEmpty() ? engineDefault : v, d))
                        parseNumber(engineDefault, d);
                    bar->setValue(d);
                };
                editor = bar;
                break;
            }
            case Kind::Toggle:
            {
                auto *box = new QComboBox;
                box->addItem(QObject::tr("On"), "1");
                box->addItem(QObject::tr("Off"), "0");
                QObject::connect(box, qOverload<int>(&QComboBox::activated), box, [write, box](int i) { write(box->itemData(i).toString()); });
                row.show = [box, engineDefault](const QString &v)
                {
                    QSignalBlocker b(box);
                    box->setCurrentIndex(std::max(0, box->findData(v.isEmpty() ? engineDefault : v)));
                };
                editor = box;
                break;
            }
            case Kind::Choice:
            {
                auto *box = new QComboBox;
                for (const auto &c : st.choices)
                    box->addItem(QObject::tr(c.second.toUtf8().constData()), c.first);
                QObject::connect(box, qOverload<int>(&QComboBox::activated), box, [write, box](int i) { write(box->itemData(i).toString()); });
                row.show = [box, engineDefault](const QString &v)
                {
                    QSignalBlocker b(box);
                    box->setCurrentIndex(std::max(0, box->findData(v.isEmpty() ? engineDefault : v)));
                };
                editor = box;
                break;
            }
            case Kind::Color:
            {
                auto *button = new QPushButton;
                button->setMinimumHeight(22);
                auto current = QSharedPointer<QVector3D>::create(parseVector(engineDefault, QVector3D(1, 1, 1)));
                row.show = [button, current, engineDefault](const QString &v)
                {
                    *current = parseVector(v.isEmpty() ? engineDefault : v, parseVector(engineDefault, QVector3D()));
                    QPixmap pm(48, 12);
                    pm.fill(toColor(*current));
                    button->setIcon(QIcon(pm));
                    button->setIconSize(pm.size());
                    button->setText(formatVector(*current));
                };
                QObject::connect(button, &QPushButton::clicked, button, [write, button, current]
                                 {
                    const QColor c = QColorDialog::getColor(toColor(*current), button, QObject::tr("Pick Color"));
                    if (c.isValid())
                        write(formatVector(QVector3D(float(c.redF()), float(c.greenF()), float(c.blueF())))); });
                editor = button;
                break;
            }
            case Kind::WheelMultiply:
            case Kind::WheelOffset:
            {
                auto *wheel = new ColorWheel(st.kind == Kind::WheelMultiply ? ColorWheel::Mode::Multiply : ColorWheel::Mode::Offset);
                QObject::connect(wheel, &ColorWheel::valueEdited, wheel, [write](const QVector3D &v, bool) { write(formatVector(v)); });
                row.show = [wheel, engineDefault](const QString &v)
                { wheel->setValue(parseVector(v.isEmpty() ? engineDefault : v, parseVector(engineDefault, QVector3D()))); };
                editor = wheel;
                break;
            }
            }

            const bool wheelRow = st.kind == Kind::WheelMultiply || st.kind == Kind::WheelOffset;
            grid->addWidget(row.toggle, gridRow, 0, wheelRow ? Qt::AlignTop : Qt::AlignVCenter);
            grid->addWidget(row.label, gridRow, 1, wheelRow ? Qt::AlignTop : Qt::AlignVCenter);
            grid->addWidget(editor, gridRow, 2);
            ++gridRow;

            QCheckBox *toggle = row.toggle;
            QLabel *label = row.label;
            auto showRow = [toggle, label, show = row.show](const QString &v)
            {
                const bool on = !v.trimmed().isEmpty();
                QSignalBlocker b(toggle);
                toggle->setChecked(on);
                label->setStyleSheet(on ? QString() : QString("color: %1;").arg(kes::style::muted().name()));
                show(v);
            };
            showRow(stored);
            QObject::connect(toggle, &QCheckBox::toggled, toggle, [host, key, engineDefault, show = row.show](bool on)
                             {
                if (on)
                    host.set(key, engineDefault);
                else
                {
                    host.remove(key);
                    show(QString());
                } });
            host.bind(key, [showRow, refreshHeader](const QString &v)
                      {
                showRow(v);
                refreshHeader(); });
            rows->push_back(row);
        }
        refreshHeader();

        QVector<QPair<QString, QString>> defaults;
        for (const Setting &st : section.settings)
            defaults.push_back({st.key, st.engineDefault});
        QObject::connect(sectionToggle, &QCheckBox::clicked, sectionToggle, [rows, host, defaults, refreshHeader, title = QString(section.title)](bool)
                         {
            // Partial or none -> all on (missing ones get the engine default); all on -> all off.
            bool all = true;
            for (const Row &r : *rows)
                all = all && r.toggle->isChecked();
            host.editKeys([&](QMap<QString, QString> &kv)
                          {
                for (const auto &d : defaults)
                {
                    if (all)
                        kv.remove(d.first);
                    else if (kv.value(d.first).trimmed().isEmpty())
                        kv[d.first] = d.second;
                } },
                          all ? QObject::tr("Clear %1 Overrides").arg(title) : QObject::tr("Override %1").arg(title));
            refreshHeader(); });

        // Expanded by default when something is overridden.
        bool anyOverride = false;
        for (const QString &k : keys)
            anyOverride = anyOverride || !o.kv.value(k).trimmed().isEmpty();
        const QString stateKey = section.title;
        const bool expanded = expandedSections().value(stateKey, anyOverride);
        body->setVisible(expanded);
        arrow->setArrowType(expanded ? Qt::DownArrow : Qt::RightArrow);
        QObject::connect(arrow, &QToolButton::clicked, arrow, [arrow, body, stateKey]
                         {
            const bool open = !body->isVisible();
            body->setVisible(open);
            arrow->setArrowType(open ? Qt::DownArrow : Qt::RightArrow);
            expandedSections()[stateKey] = open; });
    }
    return root;
}
