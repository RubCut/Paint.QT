#include "ui/ColorWheel.h"
#include "core/ColorUtils.h"

#include <QImage>
#include <QMouseEvent>
#include <QPainter>
#include <QtMath>

namespace pnq {

namespace {
QImage* wheelCache()
{
    static QImage cache;
    return &cache;
}
const int kCacheDiameter = 220;
} // namespace

ColorWheel::ColorWheel(QWidget* parent) : QWidget(parent)
{
    setMinimumSize(150, 150);
    setCursor(Qt::CrossCursor);
    setToolTip(tr("Click or drag to pick a hue and saturation"));
}

QImage ColorWheel::renderWheel() const
{
    QImage& cache = *wheelCache();
    if (!cache.isNull() && cache.width() == kCacheDiameter)
        return cache;

    const int d = kCacheDiameter;
    cache = QImage(d, d, QImage::Format_ARGB32_Premultiplied);
    cache.fill(Qt::transparent);
    QPainter p(&cache);
    p.setRenderHint(QPainter::SmoothPixmapTransform, false);
    const double r = d / 2.0;
    for (int y = 0; y < d; ++y) {
        QRgb* row = reinterpret_cast<QRgb*>(cache.scanLine(y));
        for (int x = 0; x < d; ++x) {
            const double dx = x + 0.5 - r;
            const double dy = y + 0.5 - r;
            const double dist = std::hypot(dx, dy);
            if (dist > r - 1.0)
                continue;
            // -90 degrees puts red at the top, as in Paint.NET.
            double angle = std::atan2(dx, -dy) * 180.0 / M_PI;
            if (angle < 0)
                angle += 360.0;
            const int hue = int(angle) % 360;
            const int sat = qBound(0, int(dist * 255.0 / (r - 1.0)), 255);
            int cr, cg, cb;
            hsvToRgbInt(hue, sat, 255, &cr, &cg, &cb);
            row[x] = qPremult(255, quint8(cr), quint8(cg), quint8(cb));
        }
    }
    return cache;
}

void ColorWheel::setColor(const QColor& c)
{
    m_updating = true;
    m_color = c;
    const QColor hsv = c.toHsv();
    m_hue = hsv.hue() < 0 ? 0 : hsv.hue();
    m_sat = hsv.saturation();
    m_value = hsv.value();
    m_updating = false;
    update();
}

void ColorWheel::setHueSat(int hue, int sat)
{
    if (m_updating)
        return;
    m_hue = qBound(0, hue, 359);
    m_sat = qBound(0, sat, 255);
    int r, g, b;
    hsvToRgbInt(m_hue, m_sat, m_value, &r, &g, &b);
    m_color = QColor(r, g, b);
    update();
    emit colorPicked(m_color);
}

void ColorWheel::setValue(int v)
{
    if (m_updating)
        return;
    m_value = qBound(0, v, 255);
    int r, g, b;
    hsvToRgbInt(m_hue, m_sat, m_value, &r, &g, &b);
    m_color = QColor(r, g, b);
    emit valueChanged(m_value);
    emit colorPicked(m_color);
}

void ColorWheel::pick(const QPointF& pos)
{
    const double r = std::min(width(), height()) / 2.0;
    double dx = pos.x() - width() / 2.0;
    double dy = pos.y() - height() / 2.0;
    const double dist = std::hypot(dx, dy);
    if (dist > r - 1.0) {
        // Clamp to the rim instead of ignoring clicks outside the circle.
        const double k = (r - 1.5) / dist;
        dx *= k;
        dy *= k;
    }
    double angle = std::atan2(dx, -dy) * 180.0 / M_PI;
    if (angle < 0)
        angle += 360.0;
    setHueSat(int(angle) % 360, qBound(0, int(std::hypot(dx, dy) * 255.0 / (r - 1.0)), 255));
}

void ColorWheel::mousePressEvent(QMouseEvent* e)
{
    pick(e->position());
}

void ColorWheel::mouseMoveEvent(QMouseEvent* e)
{
    if (e->buttons() & Qt::LeftButton)
        pick(e->position());
}

void ColorWheel::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);
    const int side = qMin(width(), height());
    const QRectF target((width() - side) / 2.0, (height() - side) / 2.0, side, side);

    // Apply the value (brightness) to the cached full-brightness wheel.
    const QImage base = renderWheel();
    QImage toned = base;
    if (m_value != 255) {
        toned = base.copy();
        for (int y = 0; y < toned.height(); ++y) {
            QRgb* row = reinterpret_cast<QRgb*>(toned.scanLine(y));
            for (int x = 0; x < toned.width(); ++x) {
                if (qAlpha(row[x]) == 0)
                    continue;
                const QColor c = QColor::fromRgba(row[x]);
                // The wheel is fully opaque, so a plain scale is enough.
                const int nr = qBound(0, c.red() * m_value / 255, 255);
                const int ng = qBound(0, c.green() * m_value / 255, 255);
                const int nb = qBound(0, c.blue() * m_value / 255, 255);
                row[x] = qPremult(255, quint8(nr), quint8(ng), quint8(nb));
            }
        }
    }
    p.drawImage(target, toned, QRectF(toned.rect()));

    // Selection marker at (hue, saturation).
    const double r = side / 2.0;
    const double angle = m_hue * M_PI / 180.0;
    const double dist = m_sat / 255.0 * (r - 1.5);
    const QPointF cx = target.center();
    const QPointF marker(cx.x() + std::sin(angle) * dist, cx.y() - std::cos(angle) * dist);
    p.setPen(QPen(QColor(255, 255, 255, 220), 2));
    p.setBrush(Qt::NoBrush);
    p.drawEllipse(marker, 6, 6);
    p.setPen(QPen(QColor(20, 20, 20, 200), 1));
    p.drawEllipse(marker, 7.5, 7.5);
}

} // namespace pnq
