#pragma once

#include <QImage>
#include <QRect>
#include <QtGlobal>

namespace pnq {

using pixel_t = quint32; ///< 0xAARRGGBB, premultiplied.

/// Owning wrapper around a premultiplied 32-bit ARGB pixel buffer.
/// QImage's implicit sharing gives us cheap snapshots; scanLine() detaches.
class Surface
{
public:
    Surface() = default;
    Surface(int w, int h);
    Surface(const QSize& s) : m_img(qMax(0, s.width()), qMax(0, s.height()),
                                   QImage::Format_ARGB32_Premultiplied)
    {
        m_img.fill(0);
    }
    explicit Surface(const QImage& img) : m_img(img) {}

    static Surface fromQImage(const QImage& img);
    static Surface fromFormat(const QImage& img); ///< converts to ARGB32_Premultiplied

    bool isNull() const { return m_img.isNull(); }
    int width() const { return m_img.width(); }
    int height() const { return m_img.height(); }
    QSize size() const { return m_img.size(); }
    QRect bounds() const { return QRect(0, 0, m_img.width(), m_img.height()); }
    quint32 devicePixelRatio() const { return m_img.devicePixelRatio(); }

    pixel_t* scanLine(int y);
    const pixel_t* scanLine(int y) const;
    pixel_t* bits() { return reinterpret_cast<pixel_t*>(scanLine(0)); }
    const pixel_t* bits() const { return reinterpret_cast<const pixel_t*>(scanLine(0)); }
    int stride() const { return m_img.bytesPerLine() / 4; }

    pixel_t pixel(int x, int y);
    pixel_t pixel(int x, int y) const;
    void setPixel(int x, int y, pixel_t p);

    void fill(pixel_t p);
    void clear();
    /// Deep copy.
    Surface copy() const { return Surface(m_img.copy()); }
    /// Forces a detach (copy-on-write) if shared.
    void detach();

    QImage toQImage() const;                  ///< ARGB32 (not premultiplied) - for saving
    QImage toQImageConst() const { return m_img; } ///< premultiplied, shared

    /// Source-over compositing of a single pixel, clipped to `clip`.
    void blendPixel(int x, int y, pixel_t src, const QRect& clip);
    void blendSpan(int y, const QRect& xrange, const pixel_t* src, const QRect& clip);
    /// Copies pixels (no blending) from src with srcOffset, clipped.
    void copyFrom(const Surface& src, const QPoint& dstPos, const QRect& rect) const;
    /// Source-over paste of src, clipped. Transparent pixels in the source
    /// leave the destination alone instead of erasing it, which is what a
    /// clipboard image copied from a selection has to do.
    void blendFrom(const Surface& src, const QPoint& dstPos, const QRect& rect) const;

    void fillRect(const QRect& r, pixel_t p);
    void blendRect(const QRect& r, pixel_t p);
    /// Resamples with SmoothTransformation.
    Surface scaled(int w, int h, bool smooth = true) const;
    Surface transformed(const QTransform& t, bool smooth = true) const;
    /// Crop; out of bounds areas are transparent.
    Surface cropped(const QRect& r) const;
    QImage scaledImage(int w, int h, bool smooth = true) const
    {
        return m_img.scaled(w, h, Qt::IgnoreAspectRatio,
                            smooth ? Qt::SmoothTransformation : Qt::FastTransformation);
    }

    bool operator==(const Surface& o) const;
    bool operator!=(const Surface& o) const { return !(*this == o); }

    QImage& image() { return m_img; }
    const QImage& image() const { return m_img; }

private:
    QImage m_img;
};

} // namespace pnq
