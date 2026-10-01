#include "core/Selection.h"
#include "core/ImageMath.h"

#include <QPainter>
#include <QQueue>
#include <QStack>
#include <QtMath>
#include <cmath>

namespace pnq {

Selection::Selection(int w, int h, bool filled)
    : m_mask(qMax(0, w), qMax(0, h), QImage::Format_Alpha8)
{
    m_mask.fill(filled ? 255 : 0);
}

void Selection::ensure(const QSize& s)
{
    if (m_mask.size() == s && m_mask.format() == QImage::Format_Alpha8)
        return;
    QImage old = m_mask;
    m_mask = QImage(s, QImage::Format_Alpha8);
    m_mask.fill(0);
    if (!old.isNull()) {
        QPainter p(&m_mask);
        p.drawImage(0, 0, old);
    }
}

quint8 Selection::at(int x, int y) const
{
    if (m_mask.isNull())
        return 255;
    if (x < 0 || y < 0 || x >= m_mask.width() || y >= m_mask.height())
        return 0;
    return quint8(m_mask.constScanLine(y)[x]);
}

int Selection::valueAt(int x, int y) const
{
    return at(x, y);
}

void Selection::setAt(int x, int y, quint8 v)
{
    if (x < 0 || y < 0 || x >= m_mask.width() || y >= m_mask.height())
        return;
    m_mask.scanLine(y)[x] = v;
}

QRect Selection::nonEmptyRect() const
{
    if (m_mask.isNull())
        return m_mask.rect();
    QRect r;
    for (int y = 0; y < m_mask.height(); ++y) {
        const uchar* row = m_mask.constScanLine(y);
        int minX = -1, maxX = -1;
        for (int x = 0; x < m_mask.width(); ++x) {
            if (row[x] != 0) {
                if (minX < 0)
                    minX = x;
                maxX = x;
            }
        }
        if (minX >= 0)
            r = r.isNull() ? QRect(minX, y, maxX - minX + 1, 1) : r.united(QRect(minX, y, maxX - minX + 1, 1));
    }
    return r;
}

Selection& Selection::operator+=(const Selection& o)
{
    if (o.isNull())
        return *this;
    ensure(o.size());
    for (int y = 0; y < height(); ++y) {
        uchar* a = m_mask.scanLine(y);
        const uchar* b = o.m_mask.constScanLine(y);
        for (int x = 0; x < width(); ++x)
            a[x] = qMax(a[x], b[x]);
    }
    return *this;
}

Selection& Selection::operator-=(const Selection& o)
{
    if (o.isNull())
        return *this;
    ensure(o.size());
    for (int y = 0; y < height(); ++y) {
        uchar* a = m_mask.scanLine(y);
        const uchar* b = o.m_mask.constScanLine(y);
        for (int x = 0; x < width(); ++x)
            a[x] = quint8(qMax(0, a[x] - b[x]));
    }
    return *this;
}

Selection& Selection::operator&=(const Selection& o)
{
    if (o.isNull())
        return *this;
    ensure(o.size());
    for (int y = 0; y < height(); ++y) {
        uchar* a = m_mask.scanLine(y);
        const uchar* b = o.m_mask.constScanLine(y);
        for (int x = 0; x < width(); ++x)
            a[x] = quint8(qMin(a[x], b[x]));
    }
    return *this;
}

Selection& Selection::operator|=(const Selection& o)
{
    if (o.isNull()) {
        m_mask = QImage();
        return *this;
    }
    ensure(o.size());
    for (int y = 0; y < height(); ++y) {
        uchar* a = m_mask.scanLine(y);
        const uchar* b = o.m_mask.constScanLine(y);
        for (int x = 0; x < width(); ++x)
            a[x] = qMax(a[x], b[x]);
    }
    return *this;
}

Selection& Selection::operator^=(const Selection& o)
{
    if (o.isNull())
        return *this;
    ensure(o.size());
    for (int y = 0; y < height(); ++y) {
        uchar* a = m_mask.scanLine(y);
        const uchar* b = o.m_mask.constScanLine(y);
        for (int x = 0; x < width(); ++x)
            a[x] = quint8(qAbs(int(a[x]) - int(b[x])));
    }
    return *this;
}

Selection Selection::operator*(const Selection& o) const
{
    Selection r(*this);
    r &= o;
    return r;
}

void Selection::invert()
{
    if (m_mask.isNull())
        return;
    for (int y = 0; y < m_mask.height(); ++y) {
        uchar* row = m_mask.scanLine(y);
        for (int x = 0; x < m_mask.width(); ++x)
            row[x] = quint8(255 - row[x]);
    }
}

void Selection::invert(const QRect& bounds)
{
    if (m_mask.isNull())
        return;
    QRect r = bounds.intersected(m_mask.rect());
    for (int y = r.top(); y <= r.bottom(); ++y) {
        uchar* row = m_mask.scanLine(y);
        for (int x = r.left(); x <= r.right(); ++x)
            row[x] = quint8(255 - row[x]);
    }
}

void Selection::intersect(const QRect& b)
{
    if (m_mask.isNull()) {
        selectRect(b);
        return;
    }
    QRect r = b.intersected(m_mask.rect());
    for (int y = 0; y < m_mask.height(); ++y) {
        uchar* row = m_mask.scanLine(y);
        bool insideRow = r.isNull() ? false : (y >= r.top() && y <= r.bottom());
        for (int x = 0; x < m_mask.width(); ++x) {
            if (!insideRow || x < r.left() || x > r.right())
                row[x] = 0;
        }
    }
}

void Selection::unite(const QRect& b)
{
    if (b.isEmpty())
        return;
    ensure(b.size());
    QPainter p(&m_mask);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(255, 255, 255, 255));
    p.drawRect(b);
}

void Selection::clear(const QRect& b)
{
    if (m_mask.isNull())
        return;
    QRect r = b.intersected(m_mask.rect());
    for (int y = r.top(); y <= r.bottom(); ++y)
        memset(m_mask.scanLine(y) + r.left(), 0, r.width());
}

void Selection::exclude(const QRect& b)
{
    if (m_mask.isNull()) {
        ensure(b.size());
    }
    clear(b);
}

void Selection::invertOn(const QRect& b)
{
    ensure(b.size());
    invert(b);
}

void Selection::grow(int amount)
{
    if (m_mask.isNull() || amount <= 0)
        return;
    if (amount < 0) {
        contract(-amount);
        return;
    }
    ImageMath::dilate(m_mask, amount);
}

void Selection::contract(int amount)
{
    if (m_mask.isNull() || amount <= 0)
        return;
    if (amount < 0) {
        grow(-amount);
        return;
    }
    ImageMath::erode(m_mask, amount);
}

void Selection::feather(int radius)
{
    if (m_mask.isNull() || radius <= 0)
        return;
    ImageMath::gaussianBlurAlpha(m_mask, radius);
}

void Selection::border(int borderWidth)
{
    if (m_mask.isNull() || borderWidth <= 0)
        return;
    QImage e(m_mask.size(), QImage::Format_Alpha8);
    e.fill(0);
    ImageMath::erode(m_mask, borderWidth, &e);
    for (int y = 0; y < height(); ++y) {
        uchar* a = m_mask.scanLine(y);
        const uchar* b = e.constScanLine(y);
        uchar* d = e.scanLine(y);
        for (int x = 0; x < width(); ++x)
            d[x] = quint8(qMax(0, a[x] - b[x]));
    }
    m_mask = e;
}

void Selection::selectRect(const QRect& r)
{
    // Keep the existing mask dimensions (they describe the document size);
    // only allocate when the selection has never been initialised.
    if (m_mask.isNull())
        ensure(r.size());
    m_mask.fill(0);
    QPainter p(&m_mask);
    p.fillRect(r, QColor(255, 255, 255, 255));
}

void Selection::selectEllipse(const QRect& r)
{
    if (m_mask.isNull())
        ensure(r.size());
    m_mask.fill(0);
    QPainter p(&m_mask);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(255, 255, 255, 255));
    p.drawEllipse(r);
}

void Selection::selectPath(const QPainterPath& path)
{
    QRectF rf = path.boundingRect();
    QRect r(qFloor(rf.left()), qFloor(rf.top()), qCeil(rf.width()) + 1, qCeil(rf.height()) + 1);
    ensure(r.size());
    m_mask.fill(0);
    QPainter p(&m_mask);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(255, 255, 255, 255));
    p.drawPath(path);
}

void Selection::selectPolygon(const QVector<QPoint>& points)
{
    QPolygon poly(points);
    QRectF rf = poly.boundingRect();
    QRect r(qFloor(rf.left()), qFloor(rf.top()), qCeil(rf.width()) + 1, qCeil(rf.height()) + 1);
    ensure(r.size());
    m_mask.fill(0);
    QPainter p(&m_mask);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(255, 255, 255, 255));
    p.drawPolygon(poly);
}

void Selection::selectPolygon(QPolygon polygon)
{
    QRectF rf = polygon.boundingRect();
    QRect r(qFloor(rf.left()), qFloor(rf.top()), qCeil(rf.width()) + 1, qCeil(rf.height()) + 1);
    ensure(r.size());
    m_mask.fill(0);
    QPainter p(&m_mask);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(255, 255, 255, 255));
    p.drawPolygon(polygon);
}

void Selection::selectTransparent(const QImage& image)
{
    ensure(image.size());
    m_mask.fill(0);
    for (int y = 0; y < image.height(); ++y) {
        const QRgb* s = reinterpret_cast<const QRgb*>(image.constScanLine(y));
        uchar* d = m_mask.scanLine(y);
        for (int x = 0; x < image.width(); ++x)
            d[x] = qAlpha(s[x]) == 0 ? 255 : 0;
    }
}

void Selection::selectOpaque(const QImage& image)
{
    ensure(image.size());
    m_mask.fill(0);
    for (int y = 0; y < image.height(); ++y) {
        const QRgb* s = reinterpret_cast<const QRgb*>(image.constScanLine(y));
        uchar* d = m_mask.scanLine(y);
        for (int x = 0; x < image.width(); ++x)
            d[x] = qAlpha(s[x]) == 255 ? 255 : 0;
    }
}

void Selection::magicWand(const QImage& image, const QPoint& seed, int tolerance, bool contiguous)
{
    const int w = image.width(), h = image.height();
    ensure(image.size());
    m_mask.fill(0);
    if (seed.x() < 0 || seed.y() < 0 || seed.x() >= w || seed.y() >= h)
        return;
    QRgb target = reinterpret_cast<const QRgb*>(image.constScanLine(seed.y()))[seed.x()];
    quint8 ta = quint8(qAlpha(target));
    if (contiguous) {
        QVector<bool> visited(w * h, false);
        QStack<QPoint> stack;
        stack.push(seed);
        visited[seed.y() * w + seed.x()] = true;
        while (!stack.isEmpty()) {
            QPoint p = stack.pop();
            m_mask.scanLine(p.y())[p.x()] = 255;
            const QRgb cur = reinterpret_cast<const QRgb*>(image.constScanLine(p.y()))[p.x()];
            const int d = int(0.299 * qAbs(qRed(cur) - qRed(target)) + 0.587 * qAbs(qGreen(cur) - qGreen(target))
                             + 0.114 * qAbs(qBlue(cur) - qBlue(target)));
            const int da = qAbs(int(qAlpha(cur)) - int(ta));
            if (d > tolerance || da > tolerance) {
                m_mask.scanLine(p.y())[p.x()] = 0;
                continue;
            }
            const QPoint n[4] = { QPoint(p.x() + 1, p.y()), QPoint(p.x() - 1, p.y()),
                                  QPoint(p.x(), p.y() + 1), QPoint(p.x(), p.y() - 1) };
            for (const QPoint& q : n) {
                if (q.x() < 0 || q.y() < 0 || q.x() >= w || q.y() >= h)
                    continue;
                if (visited[q.y() * w + q.x()])
                    continue;
                visited[q.y() * w + q.x()] = true;
                stack.push(q);
            }
        }
    } else {
        for (int y = 0; y < h; ++y) {
            const QRgb* row = reinterpret_cast<const QRgb*>(image.constScanLine(y));
            uchar* d = m_mask.scanLine(y);
            for (int x = 0; x < w; ++x) {
                const int dist = int(0.299 * qAbs(qRed(row[x]) - qRed(target))
                                     + 0.587 * qAbs(qGreen(row[x]) - qGreen(target))
                                     + 0.114 * qAbs(qBlue(row[x]) - qBlue(target)));
                const int da = qAbs(int(qAlpha(row[x])) - int(ta));
                d[x] = (dist <= tolerance && da <= tolerance) ? 255 : 0;
            }
        }
    }
}

QPainterPath Selection::pathFromMask(int threshold) const
{
    QPainterPath p;
    if (isNull())
        return p;
    // Marching squares based outline extraction on the mask.
    const int w = width(), h = height();
    for (int y = 0; y < h; ++y) {
        const uchar* row = constScanLineSafe(y);
        int start = -1;
        for (int x = 0; x <= w; ++x) {
            bool on = x < w && row[x] >= threshold;
            if (on && start < 0)
                start = x;
            else if (!on && start >= 0) {
                p.addRect(QRectF(start, y, x - start, 1));
                start = -1;
            }
        }
    }
    return p;
}

const uchar* Selection::constScanLineSafe(int y) const
{
    return reinterpret_cast<const uchar*>(m_mask.constScanLine(y));
}

bool Selection::isEmpty() const
{
    if (m_mask.isNull())
        return false;
    for (int y = 0; y < m_mask.height(); ++y) {
        const uchar* row = m_mask.constScanLine(y);
        for (int x = 0; x < m_mask.width(); ++x)
            if (row[x] != 0)
                return false;
    }
    return true;
}

bool Selection::operator==(const Selection& o) const
{
    if (m_mask.isNull() != o.m_mask.isNull())
        return false;
    if (m_mask.isNull())
        return true;
    if (m_mask.size() != o.m_mask.size())
        return false;
    for (int y = 0; y < m_mask.height(); ++y)
        if (memcmp(m_mask.constScanLine(y), o.m_mask.constScanLine(y), m_mask.width()) != 0)
            return false;
    return true;
}

} // namespace pnq
