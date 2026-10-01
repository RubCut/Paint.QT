#pragma once

#include "core/ColorUtils.h"
#include "core/Surface.h"

#include <QColor>
#include <QImage>
#include <QJsonObject>
#include <QPointF>
#include <QString>
#include <QVector>

namespace pnq {

class Selection;

enum class GradientMode {
    Linear = 0,
    Circular,
    Reflected,
    Rhombus
};

QString gradientModeName(GradientMode m);

struct ColorStop {
    double position = 0.0; ///< 0..1
    pixel_t color = 0xFF000000u;
    QColor qcolor() const { return toQColor(color); }
    bool operator==(const ColorStop& o) const
    {
        return qFuzzyCompare(position + 1.0, o.position + 1.0) && color == o.color;
    }
};

/// A Paint.NET style gradient: an ordered list of colour stops.
class Gradient
{
public:
    Gradient();
    explicit Gradient(const QVector<ColorStop>& stops, GradientMode mode = GradientMode::Linear);

    static Gradient fromTwoColors(pixel_t a, pixel_t b, GradientMode mode = GradientMode::Linear);
    static Gradient fromBitmap(const QImage& row);
    static Gradient presets(int index);
    static QVector<Gradient> allPresets();

    const QVector<ColorStop>& stops() const { return m_stops; }
    void setStops(const QVector<ColorStop>& s);
    void addStop(double position, pixel_t color);
    void removeStop(int index);
    void moveStop(int from, int to);

    GradientMode mode() const { return m_mode; }
    void setMode(GradientMode m) { m_mode = m; }

    QPointF focalPoint() const { return m_focal; }
    void setFocalPoint(const QPointF& p) { m_focal = p; }

    /// 256x1 preview image of the gradient.
    QImage preview(int width = 256) const;
    QImage toBitmap() const { return preview(256); }

    pixel_t colorAt(double t) const;
    /// Bilinear sample in "gradient space" (0..1 in both axes).
    pixel_t colorAt2D(double x, double y) const;

    QJsonObject toJson() const;
    static Gradient fromJson(const QJsonObject& o);

private:
    QVector<ColorStop> m_stops;
    GradientMode m_mode = GradientMode::Linear;
    QPointF m_focal = { 0.5, 0.5 };
};

} // namespace pnq
