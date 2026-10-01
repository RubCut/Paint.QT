#pragma once

#include <QWidget>

class QSlider;
class QLabel;

namespace pnq {

/// The circular hue/saturation picker used by the Colors palette:
/// the angle around the circle is the hue, the radius the saturation.
class ColorWheel : public QWidget
{
    Q_OBJECT
public:
    explicit ColorWheel(QWidget* parent = nullptr);

    QColor color() const { return m_color; }
    void setColor(const QColor& c);
    int hue() const { return m_hue; }
    int saturation() const { return m_sat; }
    void setHueSat(int hue, int sat);
    /// Rendered value (brightness) applied to the wheel's colours.
    int value() const { return m_value; }
    void setValue(int v);
    /// Replaces the preset swatch strip shown underneath the wheel.
    void setSwatchRow(const QColor& a, const QColor& b, const QColor& c) { m_swatches = { a, b, c }; }
    bool hasSwatches() const { return !m_swatches.isEmpty(); }

signals:
    void colorPicked(const QColor& c);
    void valueChanged(int v);

protected:
    void paintEvent(QPaintEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;

private:
    void pick(const QPointF& pos);
    QImage renderWheel() const;

    QColor m_color = Qt::white;
    int m_hue = 0;
    int m_sat = 0;
    int m_value = 255;
    bool m_updating = false;
    QList<QColor> m_swatches;
};

} // namespace pnq
