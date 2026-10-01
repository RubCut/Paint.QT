#include "core/Surface.h"
#include "core/ColorUtils.h"

#include <QTransform>

namespace pnq {

Surface::Surface(int w, int h)
    : m_img(qMax(0, w), qMax(0, h), QImage::Format_ARGB32_Premultiplied)
{
    // QImage does not zero its memory: a fresh Surface must start fully
    // transparent, otherwise every compositing result is undefined.
    m_img.fill(0);
}

Surface Surface::fromQImage(const QImage& img)
{
    Surface s;
    s.m_img = img;
    return s;
}

Surface Surface::fromFormat(const QImage& img)
{
    Surface s;
    if (img.format() == QImage::Format_ARGB32_Premultiplied && img.devicePixelRatio() == 1.0)
        s.m_img = img;
    else
        s.m_img = img.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    s.m_img.setDevicePixelRatio(1.0);
    return s;
}

pixel_t* Surface::scanLine(int y)
{
    if (m_img.isNull() || y < 0 || y >= m_img.height())
        return nullptr;
    if (m_img.isDetached())
        return reinterpret_cast<pixel_t*>(m_img.scanLine(y));
    m_img = m_img.copy();
    return reinterpret_cast<pixel_t*>(m_img.scanLine(y));
}

const pixel_t* Surface::scanLine(int y) const
{
    if (m_img.isNull() || y < 0 || y >= m_img.height())
        return nullptr;
    return reinterpret_cast<const pixel_t*>(m_img.constScanLine(y));
}

pixel_t Surface::pixel(int x, int y)
{
    if (x < 0 || y < 0 || x >= m_img.width() || y >= m_img.height())
        return 0;
    pixel_t* p = scanLine(y);
    return p[x];
}

pixel_t Surface::pixel(int x, int y) const
{
    if (x < 0 || y < 0 || x >= m_img.width() || y >= m_img.height())
        return 0;
    const pixel_t* p = scanLine(y);
    return p[x];
}

void Surface::setPixel(int x, int y, pixel_t p)
{
    if (x < 0 || y < 0 || x >= m_img.width() || y >= m_img.height())
        return;
    pixel_t* row = scanLine(y);
    row[x] = p;
}

void Surface::fill(pixel_t p)
{
    if (!m_img.isNull())
        m_img.fill(p);
}

void Surface::clear()
{
    fill(0);
}

void Surface::detach()
{
    if (!m_img.isDetached())
        m_img = m_img.copy();
}

QImage Surface::toQImage() const
{
    if (m_img.isNull())
        return QImage();
    return m_img.convertToFormat(QImage::Format_ARGB32);
}

void Surface::blendPixel(int x, int y, pixel_t src, const QRect& clip)
{
    if (!clip.contains(x, y) || (src >> 24) == 0)
        return;
    pixel_t* row = scanLine(y);
    pixel_t dst = row[x];
    row[x] = composePixel(dst, src, BlendMode::Normal);
}

void Surface::blendFrom(const Surface& src, const QPoint& dstPos, const QRect& rect) const
{
    if (rect.isEmpty() || src.isNull())
        return;
    // Same clipping rules as copyFrom, but the source is composited over the
    // destination instead of overwriting it.
    const int x0 = qMax(0, rect.left());
    const int y0 = qMax(0, rect.top());
    const int x1 = qMin(src.width() - 1, rect.right());
    const int y1 = qMin(src.height() - 1, rect.bottom());
    if (x1 < x0 || y1 < y0)
        return;
    for (int sy = y0; sy <= y1; ++sy) {
        const int dy = dstPos.y() + (sy - rect.top());
        if (dy < 0 || dy >= m_img.height())
            continue;
        const pixel_t* s = src.scanLine(sy);
        pixel_t* d = const_cast<Surface*>(this)->scanLine(dy);
        if (!s || !d)
            continue;
        for (int sx = x0; sx <= x1; ++sx) {
            const int dx = dstPos.x() + (sx - rect.left());
            if (dx < 0 || dx >= m_img.width())
                continue;
            d[dx] = composePixel(d[dx], s[sx], BlendMode::Normal);
        }
    }
}

void Surface::blendSpan(int y, const QRect& xrange, const pixel_t* src, const QRect& clip)
{
    if (y < 0 || y >= m_img.height() || !src)
        return;
    QRect xr = xrange.intersected(clip);
    if (xr.width() <= 0)
        return;
    pixel_t* row = scanLine(y);
    const pixel_t* s = src + (xr.left() - xrange.left());
    pixel_t* d = row + xr.left();
    for (int i = 0; i < xr.width(); ++i)
        d[i] = composePixel(d[i], s[i], BlendMode::Normal);
}

void Surface::copyFrom(const Surface& src, const QPoint& dstPos, const QRect& rect) const
{
    if (rect.isEmpty() || src.isNull())
        return;
    // `rect` is expressed in source coordinates; `dstPos` is where its top-left
    // corner lands. Everything outside either surface is clipped away.
    const int x0 = qMax(0, rect.left());
    const int y0 = qMax(0, rect.top());
    const int x1 = qMin(src.width() - 1, rect.right());
    const int y1 = qMin(src.height() - 1, rect.bottom());
    if (x1 < x0 || y1 < y0)
        return;
    for (int sy = y0; sy <= y1; ++sy) {
        const int dy = dstPos.y() + (sy - rect.top());
        if (dy < 0 || dy >= m_img.height())
            continue;
        const pixel_t* s = src.scanLine(sy);
        pixel_t* d = const_cast<Surface*>(this)->scanLine(dy);
        if (!s || !d)
            continue;
        for (int sx = x0; sx <= x1; ++sx) {
            const int dx = dstPos.x() + (sx - rect.left());
            if (dx < 0 || dx >= m_img.width())
                continue;
            d[dx] = s[sx];
        }
    }
}

void Surface::fillRect(const QRect& r, pixel_t p)
{
    QRect rr = r.intersected(bounds());
    if (rr.isEmpty())
        return;
    for (int y = rr.top(); y <= rr.bottom(); ++y) {
        pixel_t* row = scanLine(y);
        if (!row)
            continue;
        for (int x = rr.left(); x <= rr.right(); ++x)
            row[x] = p;
    }
}

void Surface::blendRect(const QRect& r, pixel_t p)
{
    QRect rr = r.intersected(bounds());
    if (rr.isEmpty() || (p >> 24) == 0)
        return;
    for (int y = rr.top(); y <= rr.bottom(); ++y) {
        pixel_t* row = scanLine(y);
        if (!row)
            continue;
        for (int x = rr.left(); x <= rr.right(); ++x)
            row[x] = composePixel(row[x], p, BlendMode::Normal);
    }
}

Surface Surface::scaled(int w, int h, bool smooth) const
{
    Surface s;
    if (isNull())
        return s;
    s.m_img = m_img.scaled(qMax(1, w), qMax(1, h), Qt::IgnoreAspectRatio,
                           smooth ? Qt::SmoothTransformation : Qt::FastTransformation);
    s.m_img.setDevicePixelRatio(1.0);
    return s;
}

Surface Surface::transformed(const QTransform& t, bool smooth) const
{
    Surface s;
    if (isNull())
        return s;
    s.m_img = m_img.transformed(t, smooth ? Qt::SmoothTransformation : Qt::FastTransformation);
    s.m_img.setDevicePixelRatio(1.0);
    return s;
}

Surface Surface::cropped(const QRect& r) const
{
    Surface s(qMax(0, r.width()), qMax(0, r.height()));
    if (r.isEmpty())
        return s;
    // Direct pixel copy: areas outside the source stay transparent.
    for (int y = 0; y < r.height(); ++y) {
        const int sy = r.top() + y;
        if (sy < 0 || sy >= m_img.height())
            continue;
        const pixel_t* src = scanLine(sy);
        pixel_t* dst = s.scanLine(y);
        if (!src || !dst)
            continue;
        for (int x = 0; x < r.width(); ++x) {
            const int sx = r.left() + x;
            if (sx < 0 || sx >= m_img.width())
                continue;
            dst[x] = src[sx];
        }
    }
    return s;
}

bool Surface::operator==(const Surface& o) const
{
    if (isNull() && o.isNull())
        return true;
    if (isNull() != o.isNull())
        return false;
    if (m_img.size() != o.m_img.size())
        return false;
    for (int y = 0; y < m_img.height(); ++y) {
        const pixel_t* a = scanLine(y);
        const pixel_t* b = o.scanLine(y);
        if (!a || !b)
            return false;
        for (int x = 0; x < m_img.width(); ++x)
            if (a[x] != b[x])
                return false;
    }
    return true;
}

} // namespace pnq
