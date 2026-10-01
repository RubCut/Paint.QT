#pragma once

#include <QImage>
#include <QPainterPath>
#include <QRect>
#include <QVector>

namespace pnq {

/// A greyscale coverage mask: 0 = excluded, 255 = fully selected.
/// A "null" selection means "everything is selected".
class Selection
{
public:
    Selection() = default;
    Selection(int w, int h, bool filled = false);

    bool isNull() const { return m_mask.isNull(); }
    int width() const { return m_mask.width(); }
    int height() const { return m_mask.height(); }
    QRect bounds() const { return m_mask.rect(); }
    QSize size() const { return m_mask.size(); }

    /// Value in 0..255 for the given document coordinate.
    int valueAt(int x, int y) const;
    quint8 at(int x, int y) const;
    void setAt(int x, int y, quint8 v);

    const QImage& mask() const { return m_mask; }
    QImage& mask() { return m_mask; }
    void setMask(const QImage& m) { m_mask = m; }

    /// Bounds of the non-zero area (works for null selections too).
    QRect nonEmptyRect() const;

    Selection& operator+=(const Selection& o);
    Selection& operator-=(const Selection& o);
    Selection& operator&=(const Selection& o);
    Selection& operator|=(const Selection& o);
    Selection& operator^=(const Selection& o);
    Selection operator*(const Selection& o) const;

    void invert();
    void invert(const QRect& bounds);
    void intersect(const QRect& bounds);
    void unite(const QRect& bounds);
    void clear(const QRect& bounds);
    void exclude(const QRect& bounds);
    void invertOn(const QRect& bounds);

    void grow(int amount);
    void contract(int amount);
    void feather(int radius);
    void border(int width);

    /// Build from a shape.
    void selectPath(const QPainterPath& path);
    void selectRect(const QRect& r);
    void selectEllipse(const QRect& r);
    void selectPolygon(const QVector<QPoint>& points);
    void selectPolygon(QPolygon polygon);
    /// Magic wand: contiguous (or global) flood fill from a seed.
    void magicWand(const QImage& image, const QPoint& seed, int tolerance, bool contiguous);
    /// Select where alpha == 0 of the given image.
    void selectTransparent(const QImage& image);
    /// Select where alpha == 255 of the given image.
    void selectOpaque(const QImage& image);

    QPainterPath pathFromMask(int threshold = 1) const;

    const uchar* constScanLineSafe(int y) const;

    bool operator==(const Selection& o) const;
    bool operator!=(const Selection& o) const { return !(*this == o); }
    bool isEmpty() const;

    /// Reduces the mask resolution by 2 (for very large selections) - optional.
    void beginOptimize() {}

private:
    void ensure(const QSize& s);
    QImage m_mask; ///< Format_Alpha8
};

} // namespace pnq
