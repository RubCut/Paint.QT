#include "core/Gradient.h"
#include "core/Selection.h"

#include <QJsonArray>
#include <QtMath>
#include <algorithm>

namespace pnq {

QString gradientModeName(GradientMode m)
{
    switch (m) {
    case GradientMode::Linear: return QObject::tr("Linear");
    case GradientMode::Circular: return QObject::tr("Circular");
    case GradientMode::Reflected: return QObject::tr("Reflected");
    case GradientMode::Rhombus: return QObject::tr("Rhombus");
    }
    return QString();
}

Gradient::Gradient()
{
    m_stops = { { 0.0, 0xFF000000u }, { 1.0, 0xFFFFFFFFu } };
}

Gradient::Gradient(const QVector<ColorStop>& stops, GradientMode mode)
    : m_stops(stops)
    , m_mode(mode)
{
    if (m_stops.isEmpty())
        m_stops = { { 0.0, 0xFF000000u }, { 1.0, 0xFFFFFFFFu } };
}

Gradient Gradient::fromTwoColors(pixel_t a, pixel_t b, GradientMode mode)
{
    return Gradient({ { 0.0, a }, { 1.0, b } }, mode);
}

Gradient Gradient::fromBitmap(const QImage& row)
{
    QVector<ColorStop> stops;
    for (int x = 0; x < row.width(); ++x) {
        QRgb c = reinterpret_cast<const QRgb*>(row.constScanLine(0))[x];
        pixel_t p = toPixel(QColor(c));
        if (!stops.isEmpty() && stops.last().color == p)
            continue;
        stops.append({ double(x) / qMax(1, row.width() - 1), p });
    }
    if (stops.isEmpty())
        stops = { { 0.0, 0xFF000000u }, { 1.0, 0xFFFFFFFFu } };
    return Gradient(stops);
}

QVector<Gradient> Gradient::allPresets()
{
    QVector<Gradient> list;
    list.append(fromTwoColors(0xFF000000u, 0xFFFFFFFFu));
    list.append(fromTwoColors(0xFFFFFFFFu, 0xFF000000u));
    list.append(fromTwoColors(0xFFFF0000u, 0xFF0000FFu));
    list.append(fromTwoColors(0xFF00FF00u, 0xFF0000FFu));
    list.append(fromTwoColors(0xFFFFFF00u, 0xFFFF0000u));
    // Rainbow
    {
        QVector<ColorStop> s;
        for (int i = 0; i <= 6; ++i) {
            int r = 0, g = 0, b = 0;
            hsvToRgbInt(i * 60, 255, 255, &r, &g, &b);
            s.append({ double(i) / 6.0, rgbPixel(r, g, b) });
        }
        list.append(Gradient(s));
    }
    // Chrome
    {
        QVector<ColorStop> s = {
            { 0.00, 0xFF2A2A2Au }, { 0.10, 0xFF9A9A9Au }, { 0.25, 0xFFFFFFFFu },
            { 0.50, 0xFF6E6E6Eu }, { 0.75, 0xFFFFFFFFu }, { 1.00, 0xFF3C3C3Cu }
        };
        list.append(Gradient(s));
    }
    // Sunburst
    {
        QVector<ColorStop> s;
        for (int i = 0; i <= 8; ++i) {
            double t = double(i) / 8.0;
            int r = qRound(255 * t), g = qRound(180 * t), b = qRound(40 * t);
            s.append({ t, rgbPixel(qBound(0, r, 255), qBound(0, g, 255), qBound(0, b, 255)) });
        }
        list.append(Gradient(s));
    }
    // Puddle
    {
        QVector<ColorStop> s = { { 0.0, 0xFF1E3A8Au },  { 0.5, 0xFF3B82F6u },
                                 { 1.0, 0xFFDBEAFEu } };
        list.append(Gradient(s));
    }
    // Transparent fade
    {
        QVector<ColorStop> s = { { 0.0, 0xFF000000u }, { 1.0, 0x00000000u } };
        list.append(Gradient(s));
    }
    // Fades
    {
        QVector<ColorStop> s = { { 0.0, 0x00FFFFFFu }, { 1.0, 0xFF000000u } };
        list.append(Gradient(s));
    }
    // "Intense Fire"
    {
        QVector<ColorStop> s = { { 0.0, 0xFF000000u },   { 0.3, 0xFFB00000u },
                                 { 0.6, 0xFFFFD000u },   { 1.0, 0xFFFFFFFFu } };
        list.append(Gradient(s));
    }
    // "Golden Ocean"
    {
        QVector<ColorStop> s = { { 0.0, 0xFF0B3C5Du }, { 0.5, 0xFF1E88E5u }, { 1.0, 0xFF64DDD0u } };
        list.append(Gradient(s));
    }
    for (Gradient& g : list)
        g.setMode(GradientMode::Linear);
    return list;
}

Gradient Gradient::presets(int index)
{
    QVector<Gradient> l = allPresets();
    if (l.isEmpty())
        return Gradient();
    return l.at(qBound(0, index, l.size() - 1));
}

void Gradient::setStops(const QVector<ColorStop>& s)
{
    m_stops = s;
    std::stable_sort(m_stops.begin(), m_stops.end(),
                     [](const ColorStop& a, const ColorStop& b) { return a.position < b.position; });
    if (m_stops.isEmpty())
        m_stops = { { 0.0, 0xFF000000u }, { 1.0, 0xFFFFFFFFu } };
}

void Gradient::addStop(double position, pixel_t color)
{
    m_stops.append({ qBound(0.0, position, 1.0), color });
    std::stable_sort(m_stops.begin(), m_stops.end(),
                     [](const ColorStop& a, const ColorStop& b) { return a.position < b.position; });
}

void Gradient::removeStop(int index)
{
    if (index < 0 || index >= m_stops.size() || m_stops.size() <= 2)
        return;
    m_stops.removeAt(index);
}

void Gradient::moveStop(int from, int to)
{
    if (from < 0 || from >= m_stops.size() || to < 0 || to >= m_stops.size() || from == to)
        return;
    m_stops.move(from, to);
}

pixel_t Gradient::colorAt(double t) const
{
    if (m_stops.isEmpty())
        return 0xFF000000u;
    t = qBound(0.0, t, 1.0);
    if (t <= m_stops.first().position)
        return m_stops.first().color;
    if (t >= m_stops.last().position)
        return m_stops.last().color;
    for (int i = 0; i + 1 < m_stops.size(); ++i) {
        const ColorStop& a = m_stops[i];
        const ColorStop& b = m_stops[i + 1];
        if (t >= a.position && t <= b.position) {
            const double span = b.position - a.position;
            const double f = span <= 0 ? 0.0 : (t - a.position) / span;
            const double af = getA(a.color) / 255.0;
            const double bf = getA(b.color) / 255.0;
            const double alpha = af + (bf - af) * f;
            return qPremult(quint8(qRound(alpha * 255)),
                            quint8(qRound(getR(a.color) + (getR(b.color) - getR(a.color)) * f)),
                            quint8(qRound(getG(a.color) + (getG(b.color) - getG(a.color)) * f)),
                            quint8(qRound(getB(a.color) + (getB(b.color) - getB(a.color)) * f)));
        }
    }
    return m_stops.last().color;
}

pixel_t Gradient::colorAt2D(double x, double y) const
{
    double t = 0.0;
    switch (m_mode) {
    case GradientMode::Linear:
        t = x;
        break;
    case GradientMode::Circular: {
        const double dx = x - 0.5, dy = y - 0.5;
        t = 0.5 + std::atan2(dy, dx) / (2 * M_PI);
        break;
    }
    case GradientMode::Reflected:
        t = x < m_focal.x() ? (2 * x / qMax(1e-6, 2 * m_focal.x()))
                            : (2 * (1.0 - x) / qMax(1e-6, 2 * (1.0 - m_focal.x())));
        break;
    case GradientMode::Rhombus:
        t = (x + y) / 2.0;
        break;
    }
    return colorAt(t);
}

QImage Gradient::preview(int width) const
{
    QImage img(qMax(2, width), 1, QImage::Format_ARGB32);
    for (int x = 0; x < img.width(); ++x) {
        const double t = double(x) / qMax(1, img.width() - 1);
        reinterpret_cast<QRgb*>(img.scanLine(0))[x] = QColor(toQColor(colorAt(t))).rgba();
    }
    return img;
}

QJsonObject Gradient::toJson() const
{
    QJsonObject o;
    o[QStringLiteral("mode")] = int(m_mode);
    o[QStringLiteral("focalX")] = m_focal.x();
    o[QStringLiteral("focalY")] = m_focal.y();
    QJsonArray arr;
    for (const ColorStop& s : m_stops) {
        QJsonObject so;
        so[QStringLiteral("p")] = s.position;
        so[QStringLiteral("c")] = QString::number(s.color, 16);
        arr.append(so);
    }
    o[QStringLiteral("stops")] = arr;
    return o;
}

Gradient Gradient::fromJson(const QJsonObject& o)
{
    QVector<ColorStop> stops;
    for (const QJsonValue& v : o.value(QStringLiteral("stops")).toArray()) {
        const QJsonObject so = v.toObject();
        bool ok = false;
        const quint32 c = so.value(QStringLiteral("c")).toString().toUInt(&ok, 16);
        stops.append({ so.value(QStringLiteral("p")).toDouble(), ok ? c : 0xFF000000u });
    }
    Gradient g;
    if (!stops.isEmpty())
        g.setStops(stops);
    g.setMode(static_cast<GradientMode>(int(o.value(QStringLiteral("mode")).toDouble(0))));
    g.setFocalPoint(QPointF(o.value(QStringLiteral("focalX")).toDouble(0.5),
                            o.value(QStringLiteral("focalY")).toDouble(0.5)));
    return g;
}

} // namespace pnq
