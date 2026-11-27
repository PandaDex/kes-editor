#pragma once
#include <QMap>
#include <QString>
#include <QVector3D>
#include <QWidget>
#include <functional>

class QLabel;
struct MapObject;

// PostProcessVolumeEntity inspector: settings grouped into collapsible
// sections, each with an override toggle (unset keys keep the layers below),
// value bars and colour wheels for lift / gamma / gain.
struct PostProcessInspectorHost
{
    std::function<void(const QString &key, const QString &value)> set;
    std::function<void(const QString &key)> remove;
    // Several keys in one undo step.
    std::function<void(const std::function<void(QMap<QString, QString> &kv)> &edit, const QString &undoText)> editKeys;
    // Called with the key's stored value ("" = not overridden) after undo / outside edits.
    std::function<void(const QString &key, std::function<void(const QString &)> update)> bind;
};

bool isPostProcessVolumeClass(const QString &classname);
bool isPostProcessInspectorKey(const QString &key);
QWidget *buildPostProcessInspector(const MapObject &o, const PostProcessInspectorHost &host);

// Drag to scrub, double-click to type. Values outside [softMin, softMax]
// can be typed, up to [hardMin, hardMax].
class ValueBar : public QWidget
{
    Q_OBJECT
public:
    ValueBar(double softMin, double softMax, double hardMin, double hardMax, int decimals, QWidget *parent = nullptr);
    void setValue(double v);
    double value() const { return m_value; }
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

signals:
    void valueEdited(double value, bool final);

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    void mouseDoubleClickEvent(QMouseEvent *e) override;
    void keyPressEvent(QKeyEvent *e) override;

private:
    double fromX(double x) const;
    void edit(double v, bool final);
    void startTyping();

    double m_softMin, m_softMax, m_hardMin, m_hardMax;
    int m_decimals;
    double m_value = 0.0;
    bool m_dragging = false;
    bool m_moved = false;
    double m_pressX = 0.0;
    double m_pressValue = 0.0;
};

// Hue / strength disc for an RGB triple. Multiplicative wheels (gamma, gain)
// scale around 1, additive ones (lift) offset around 0; the bar below sets
// the overall level.
class ColorWheel : public QWidget
{
    Q_OBJECT
public:
    enum class Mode
    {
        Multiply,
        Offset,
    };
    explicit ColorWheel(Mode mode, QWidget *parent = nullptr);
    void setValue(const QVector3D &v);
    QVector3D value() const { return m_value; }

signals:
    void valueEdited(const QVector3D &value, bool final);

private:
    class Disc;
    void fromValue();
    void toValue(bool final);

    Mode m_mode;
    QVector3D m_value;
    float m_x = 0.f, m_y = 0.f, m_level = 1.f;
    Disc *m_disc;
    ValueBar *m_levelBar;
    QLabel *m_readout;
};
