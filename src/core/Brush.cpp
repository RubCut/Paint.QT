#include "core/Brush.h"
#include "core/ImageMath.h"

#include <QCache>
#include <QPainter>
#include <QPainterPath>
#include <QtMath>
#include <cmath>

namespace pnq {

QString brushShapeName(BrushShape s)
{
    switch (s) {
    case BrushShape::Round: return QObject::tr("Circle");
    case BrushShape::Square: return QObject::tr("Square");
    case BrushShape::ForwardSlash: return QObject::tr("Forward Slash");
    case BrushShape::BackSlash: return QObject::tr("Back Slash");
    case BrushShape::Horizontal: return QObject::tr("Horizontal");
    case BrushShape::Vertical: return QObject::tr("Vertical");
    default: return QStringLiteral("?");
    }
}

BrushShape brushShapeFromName(const QString& name)
{
    for (int i = 0; i < static_cast<int>(BrushShape::Count); ++i) {
        if (brushShapeName(static_cast<BrushShape>(i)).compare(name, Qt::CaseInsensitive) == 0)
            return static_cast<BrushShape>(i);
    }
    return BrushShape::Round;
}

QPixmap brushShapeIcon(BrushShape s, int size)
{
    QPixmap pm(size, size);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(Qt::NoPen);
    p.setBrush(Qt::black);
    const int pad = 2;
    const int w = size - pad * 2;
    switch (s) {
    case BrushShape::Round:
        p.drawEllipse(QRect(pad, pad, w, w));
        break;
    case BrushShape::Square:
        p.drawRect(QRect(pad, pad, w, w));
        break;
    case BrushShape::ForwardSlash: {
        QPainterPath path;
        path.moveTo(pad, size - pad);
        path.lineTo(size - pad, pad);
        path.lineTo(size - pad - w / 3.0, pad);
        path.lineTo(pad + w / 3.0, size - pad);
        path.closeSubpath();
        p.drawPath(path);
        break;
    }
    case BrushShape::BackSlash: {
        QPainterPath path;
        path.moveTo(pad, pad);
        path.lineTo(size - pad, size - pad);
        path.lineTo(size - pad - w / 3.0, size - pad);
        path.lineTo(pad + w / 3.0, pad);
        path.closeSubpath();
        p.drawPath(path);
        break;
    }
    case BrushShape::Horizontal:
        p.drawRect(QRect(pad, size / 2 - w / 6, w, w / 3.0));
        break;
    case BrushShape::Vertical:
        p.drawRect(QRect(size / 2 - w / 6, pad, w / 3.0, w));
        break;
    default:
        break;
    }
    return pm;
}

Brush::Brush() = default;

Brush::Brush(int size) : m_size(qBound(1, size, 1000)) {}

double Brush::spacingDistance() const
{
    if (m_spacing >= 100)
        return 0.0;
    return m_size * (m_spacing / 100.0);
}

namespace {
struct StampKey {
    int size;
    int hardness;
    int shape;
    bool aa;
    bool operator==(const StampKey& o) const
    {
        return size == o.size && hardness == o.hardness && shape == o.shape && aa == o.aa;
    }
};
uint qHash(const StampKey& k, uint seed = 0)
{
    return ::qHash(k.size * 7919 + k.hardness * 104729 + k.shape * 31 + (k.aa ? 1 : 0), seed);
}

QCache<quint32, QImage>* stampCache()
{
    // 64 MiB of cached stamps. The default QCache cost of 100 would evict
    // every stamp the moment it is inserted.
    static QCache<quint32, QImage> cache(64 * 1024 * 1024);
    return &cache;
}

quint32 stampKeyValue(int size, int hardness, BrushShape shape, bool aa)
{
    quint32 k = quint32(size) & 0x3FF;
    k |= (quint32(hardness) & 0xFF) << 10;
    k |= (quint32(int(shape)) & 0x7) << 18;
    k |= (aa ? 1u : 0u) << 21;
    return k;
}
} // namespace

void Brush::setCacheLimit(int mb)
{
    stampCache()->setMaxCost(qMax(1, mb) * 1024 * 1024);
}

QImage Brush::makeStamp(int size, int hardness, BrushShape shape, bool antiAlias, bool forceFull)
{
    size = qMax(1, size);
    const int d = size;
    QImage img(d, d, QImage::Format_Alpha8);
    img.fill(0);
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing, antiAlias && !forceFull);
    p.setPen(Qt::NoPen);
    const double hardnessFrac = hardness / 100.0;
    const double centerFrac = hardnessFrac / 2.0;
    const double edgeFrac = 1.0 - centerFrac;
    const double radius = d / 2.0;

    QBrush b;
    if (hardness >= 100 || forceFull) {
        b = QBrush(Qt::black);
    } else {
        QRadialGradient g(radius, radius, radius);
        g.setColorAt(0.0, QColor(0, 0, 0, 255));
        g.setColorAt(edgeFrac, QColor(0, 0, 0, 255));
        g.setColorAt(1.0, QColor(0, 0, 0, 0));
        b = QBrush(g);
    }
    p.setBrush(b);

    const QRectF r(0, 0, d, d);
    switch (shape) {
    case BrushShape::Round:
        p.drawEllipse(r);
        break;
    case BrushShape::Square:
        p.drawRect(r);
        break;
    case BrushShape::ForwardSlash: {
        QPainterPath path;
        path.moveTo(0, d);
        path.lineTo(d, 0);
        path.lineTo(d, -d);
        path.lineTo(-d, d);
        path.closeSubpath();
        p.save();
        p.translate(0, 0);
        p.drawPath(path);
        p.restore();
        break;
    }
    case BrushShape::BackSlash: {
        QPainterPath path;
        path.moveTo(0, 0);
        path.lineTo(d, d);
        path.lineTo(d, 2 * d);
        path.lineTo(-d, 0);
        path.closeSubpath();
        p.drawPath(path);
        break;
    }
    case BrushShape::Horizontal:
        p.drawRect(QRectF(0, d / 6.0, d, d * 2.0 / 3.0));
        break;
    case BrushShape::Vertical:
        p.drawRect(QRectF(d / 6.0, 0, d * 2.0 / 3.0, d));
        break;
    default:
        p.drawEllipse(r);
        break;
    }
    p.end();
    return img;
}

const QImage& Brush::stamp() const
{
    const quint32 key = stampKeyValue(m_size, m_hardness, m_shape, m_antiAlias);
    auto* cache = stampCache();
    if (QImage* found = cache->object(key))
        return *found;
    QImage img = makeStamp(m_size, m_hardness, m_shape, m_antiAlias);
    cache->insert(key, new QImage(img), qMax(1, img.sizeInBytes()));
    if (QImage* inserted = cache->object(key))
        return *inserted;
    // The cache refused the entry (should not happen, but never hand out a
    // dangling reference): fall back to a function-local copy.
    static QImage fallback;
    fallback = img;
    return fallback;
}

void Brush::drawPoint(Surface& target, const QPointF& pt, pixel_t color, const Selection* selection,
                      double extraOpacity) const
{
    const QImage& st = stamp();
    const int d = m_size;
    const int cx = int(std::floor(pt.x() - d / 2.0 + 0.5));
    const int cy = int(std::floor(pt.y() - d / 2.0 + 0.5));

    double op = (m_opacity / 100.0) * extraOpacity;
    if (op <= 0.0)
        return;

    const bool hasTex = !m_texture.isNull();
    for (int y = cy; y < cy + d; ++y) {
        if (y < 0 || y >= target.height())
            continue;
        pixel_t* row = target.scanLine(y);
        const uchar* srow = st.constScanLine(y - cy);
        const QRgb* trow = hasTex ? reinterpret_cast<const QRgb*>(
                              m_texture.constScanLine((y - cy) % m_texture.height()))
                                  : nullptr;
        const uchar* mrow = selection ? selection->mask().constScanLine(y) : nullptr;
        for (int x = cx; x < cx + d; ++x) {
            if (x < 0 || x >= target.width())
                continue;
            int a = srow[x - cx];
            if (mrow) {
                const int mv = mrow[x];
                if (mv == 0)
                    continue;
                a = a * mv / 255;
            }
            if (hasTex && trow) {
                // Texture is a 50% grey "no-op" image; blend multiplicatively.
                int tv = qGray(trow[(x - cx) % m_texture.width()]);
                a = a * tv / 255;
            }
            if (a == 0)
                continue;
            int alpha = int(a * op);
            if (alpha <= 0)
                continue;
            if (alpha > 255)
                alpha = 255;
            if (m_mode == BrushMode::Erase) {
                pixel_t dst = row[x];
                row[x] = qPremult(quint8(getA(dst) * (255 - alpha) / 255), quint8(getR(dst) * (255 - alpha) / 255),
                                  quint8(getG(dst) * (255 - alpha) / 255),
                                  quint8(getB(dst) * (255 - alpha) / 255));
            } else {
                row[x] = composePixel(row[x], qPremult(quint8(alpha), getR(color), getG(color),
                                                        getB(color)),
                                      BlendMode::Normal);
            }
        }
    }
}

void Brush::drawSegment(Surface& target, const QPointF& a, const QPointF& b, pixel_t color,
                        const Selection* selection, double extraOpacity) const
{
    const double dist = std::hypot(b.x() - a.x(), b.y() - a.y());
    const double step = spacingDistance();
    if (step <= 0.5 || dist <= step) {
        drawPoint(target, a, color, selection, extraOpacity);
        if (dist > 0.5)
            drawPoint(target, b, color, selection, extraOpacity);
        return;
    }
    const int n = int(std::ceil(dist / step));
    for (int i = 0; i <= n; ++i) {
        const double t = double(i) / n;
        const QPointF p(a.x() + (b.x() - a.x()) * t, a.y() + (b.y() - a.y()) * t);
        drawPoint(target, p, color, selection, extraOpacity);
    }
}

Surface Brush::renderPreview(int size) const
{
    Q_UNUSED(size);
    QImage img(m_size + 4, m_size + 4, QImage::Format_ARGB32);
    img.fill(Qt::transparent);
    QPainter p(&img);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);
    QImage st = stamp().convertToFormat(QImage::Format_ARGB32);
    p.drawImage(QRect(0, 0, st.width(), st.height()), st);
    p.end();
    return Surface::fromQImage(img);
}

void Brush::fillArea(Surface& target, const QRect& area, pixel_t color, const Selection* selection,
                     double extraOpacity) const
{
    // Tiles the brush over the whole area using the configured spacing.
    QRect r = area.normalized();
    if (r.isEmpty())
        return;
    const double step = qMax(1.0, spacingDistance());
    for (int y = r.top(); y <= r.bottom(); y += int(step)) {
        for (int x = r.left(); x <= r.right(); x += int(step))
            drawPoint(target, QPointF(x, y), color, selection, extraOpacity);
        // Make sure the right/bottom edge is covered.
        drawPoint(target, QPointF(r.right(), y), color, selection, extraOpacity);
    }
    for (int y = r.top(); y <= r.bottom(); y += int(step))
        drawPoint(target, QPointF(r.left(), y), color, selection, extraOpacity);
    drawPoint(target, QPointF(r.right(), r.bottom()), color, selection, extraOpacity);
}

QList<Brush> Brush::defaultBrushes()
{
    QList<Brush> list;
    auto add = [&](int size, int hardness, int spacing, BrushShape shape, const QString& name) {
        Brush b(size);
        b.setHardness(hardness);
        b.setSpacing(spacing);
        b.setShape(shape);
        b.setName(name);
        list.append(b);
    };
    add(1, 100, 100, BrushShape::Round, QObject::tr("Pixel Brush"));
    add(2, 100, 50, BrushShape::Round, QObject::tr("Small Brush"));
    add(4, 60, 30, BrushShape::Round, QObject::tr("Soft Round"));
    add(8, 60, 20, BrushShape::Round, QObject::tr("Pencil"));
    add(19, 50, 20, BrushShape::Round, QObject::tr("Brush 1"));
    add(19, 50, 20, BrushShape::Round, QObject::tr("Brush 2"));
    add(24, 90, 20, BrushShape::Round, QObject::tr("Hard Round"));
    add(32, 100, 10, BrushShape::Round, QObject::tr("Thin Round"));
    add(36, 30, 20, BrushShape::Round, QObject::tr("Soft Round 2"));
    add(48, 100, 10, BrushShape::Round, QObject::tr("Thick Round"));
    add(64, 30, 20, BrushShape::Round, QObject::tr("Air Brush Soft"));
    add(100, 20, 20, BrushShape::Round, QObject::tr("Air Brush"));
    add(200, 40, 20, BrushShape::Round, QObject::tr("Wide Soft"));
    add(8, 100, 20, BrushShape::Square, QObject::tr("Square Small"));
    add(16, 100, 20, BrushShape::Square, QObject::tr("Square"));
    add(19, 90, 20, BrushShape::ForwardSlash, QObject::tr("Slash"));
    add(19, 90, 20, BrushShape::BackSlash, QObject::tr("Back Slash"));
    add(19, 90, 20, BrushShape::Horizontal, QObject::tr("Horizontal"));
    add(19, 90, 20, BrushShape::Vertical, QObject::tr("Vertical"));
    return list;
}

QStringList Brush::defaultTextureNames()
{
    return { QStringLiteral("None"),         QStringLiteral("Pine Bark"),
             QStringLiteral("Rock Wall"),    QStringLiteral("Concrete"),
             QStringLiteral("Wood Grain"),   QStringLiteral("Rattan"),
             QStringLiteral("Sponge"),       QStringLiteral("Rocky"),
             QStringLiteral("Scattered Pebbles"), QStringLiteral("Weave"),
             QStringLiteral("Woven Fibers"), QStringLiteral("Cracked Pattern"),
             QStringLiteral("Rocky 2"),      QStringLiteral("Zebra Fur"),
             QStringLiteral("Waves") };
}

QImage Brush::generateTexture(const QString& name, int size, quint32 seed)
{
    if (name.compare(QLatin1String("None"), Qt::CaseInsensitive) == 0)
        return QImage();
    QImage img(size, size, QImage::Format_Grayscale8);
    img.fill(127);
    QPainter p(&img);
    p.setPen(Qt::NoPen);
    p.setBrush(Qt::NoBrush);
    std::srand(seed);
    auto rnd = [] { return double(std::rand()) / RAND_MAX; };

    if (name.contains(QLatin1String("Pine"), Qt::CaseInsensitive)) {
        for (int i = 0; i < 260; ++i) {
            int x = int(rnd() * size), y = int(rnd() * size);
            int h = 4 + rnd() * 10;
            p.setPen(QPen(QColor(60 + rnd() * 60, 0, 0), 1 + rnd() * 2));
            p.drawLine(x, y, x + (rnd() - 0.5) * 4, y - h);
            p.drawLine(x, y, x + (rnd() - 0.5) * 4, y - h / 2.0);
        }
    } else if (name.contains(QLatin1String("Wood"), Qt::CaseInsensitive)) {
        for (int x = 0; x < size; ++x) {
            int v = 100 + 60 * std::sin(x * 0.7) + rnd() * 30;
            p.setPen(QColor(qBound(0, int(v), 255), 0, 0));
            p.drawLine(x, 0, x, size);
        }
    } else if (name.contains(QLatin1String("Concrete"), Qt::CaseInsensitive)
               || name.contains(QLatin1String("Wall"), Qt::CaseInsensitive)) {
        for (int i = 0; i < 1800; ++i) {
            int v = 100 + rnd() * 80;
            p.setPen(QPen(QColor(qBound(0, int(v), 255), 0, 0), 1));
            p.drawPoint(int(rnd() * size), int(rnd() * size));
        }
    } else if (name.contains(QLatin1String("Sponge"), Qt::CaseInsensitive)) {
        for (int i = 0; i < 500; ++i) {
            int v = 60 + rnd() * 140;
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(qBound(0, int(v), 255), 0, 0));
            p.drawEllipse(int(rnd() * size), int(rnd() * size), 1 + rnd() * 4, 1 + rnd() * 4);
        }
    } else if (name.contains(QLatin1String("Pebbles"), Qt::CaseInsensitive)
               || name.contains(QLatin1String("Rocky"), Qt::CaseInsensitive)) {
        for (int i = 0; i < 260; ++i) {
            int v = 70 + rnd() * 120;
            p.setPen(QPen(QColor(0, 0, 0), 1));
            p.setBrush(QColor(qBound(0, int(v), 255), 0, 0));
            p.drawEllipse(int(rnd() * size), int(rnd() * size), 4 + rnd() * 14, 4 + rnd() * 14);
        }
    } else if (name.contains(QLatin1String("Weave"), Qt::CaseInsensitive)
               || name.contains(QLatin1String("Woven"), Qt::CaseInsensitive)) {
        for (int i = 0; i < size; i += 4) {
            p.setPen(QPen(QColor(190, 0, 0), 2));
            p.drawLine(i, 0, i, size);
            p.setPen(QPen(QColor(60, 0, 0), 2));
            p.drawLine(0, i, size, i);
        }
    } else if (name.contains(QLatin1String("Cracked"), Qt::CaseInsensitive)) {
        for (int i = 0; i < 60; ++i) {
            QPointF pt(rnd() * size, rnd() * size);
            QPainterPath path;
            path.moveTo(pt);
            for (int j = 0; j < 6; ++j)
                path.lineTo(pt.x() + (rnd() - 0.5) * 20, pt.y() + (rnd() - 0.5) * 20);
            p.setPen(QPen(QColor(50, 0, 0), 1));
            p.setBrush(Qt::NoBrush);
            p.drawPath(path);
        }
    } else if (name.contains(QLatin1String("Zebra"), Qt::CaseInsensitive)
               || name.contains(QLatin1String("Fur"), Qt::CaseInsensitive)) {
        for (int i = 0; i < 400; ++i) {
            int x = int(rnd() * size), y = int(rnd() * size);
            p.setPen(QPen(QColor(rnd() > 0.5 ? 20 : 235, 0, 0), 1 + rnd() * 2));
            p.drawLine(x, y, x + (rnd() - 0.5) * 6, y + rnd() * 10);
        }
    } else { // Waves
        for (int y = 0; y < size; ++y) {
            for (int x = 0; x < size; ++x) {
                int v = 127 + 90 * std::sin((x + y) * 0.35);
                ((uchar*)img.scanLine(y))[x] = uchar(qBound(0, v, 255));
            }
        }
    }
    p.end();
    return img;
}

QImage Brush::textureFromResource(const QString& name)
{
    return generateTexture(name);
}

} // namespace pnq
