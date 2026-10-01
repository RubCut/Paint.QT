// ==============================================================
//  Effects.cpp - core helpers, menu categories and geometry ops.
// ==============================================================
#include "effects/Effects.h"

#include "core/ImageMath.h"

#include <QPainter>
#include <QRectF>
#include <QTransform>
#include <QtMath>

#include <algorithm>
#include <cmath>

namespace pnq {
namespace Effects {
namespace {

/// Scales a premultiplied pixel by k/255. The scaling has to be done per
/// channel: scaling the packed 32 bit value would leak carries between them.
inline pixel_t scalePremult(pixel_t p, int k)
{
    if (k <= 0)
        return 0;
    if (k >= 255)
        return p;
    const quint32 a = quint32((int(chanA(p)) * k + 127) / 255);
    const quint32 r = std::min(a, quint32((int(chanR(p)) * k + 127) / 255));
    const quint32 g = std::min(a, quint32((int(chanG(p)) * k + 127) / 255));
    const quint32 b = std::min(a, quint32((int(chanB(p)) * k + 127) / 255));
    return (a << 24) | (r << 16) | (g << 8) | b;
}

} // namespace

// -------------------------------------------------------------- helpers
void forEachPixel(const Surface& src, const Selection& sel,
                  const std::function<void(int, int, pixel_t&)>& fn)
{
    if (!fn || src.isNull())
        return;

    // The signature is const for convenience of the callers, but the callback
    // is expected to modify the pixels in place.
    Surface& s = const_cast<Surface&>(src);
    const int w = s.width();
    const int h = s.height();
    if (w <= 0 || h <= 0)
        return;

    // A mask is only usable when it matches the surface exactly; anything else
    // is treated as "no selection".
    const QImage* mask = nullptr;
    if (!sel.isNull() && sel.width() == w && sel.height() == h)
        mask = &sel.mask();
    if (mask && mask->isNull())
        mask = nullptr;

    for (int y = 0; y < h; ++y) {
        pixel_t* row = s.scanLine(y);
        if (!row)
            continue;
        const uchar* mrow = mask ? mask->constScanLine(y) : nullptr;
        for (int x = 0; x < w; ++x) {
            const int v = mrow ? int(mrow[x]) : 255;
            if (v <= 0)
                continue; // outside the selection: leave the pixel alone
            pixel_t& p = row[x];
            if (v < 255) {
                // Premultiplied: scaling every channel also scales the alpha.
                p = scalePremult(p, v);
                if (p == 0)
                    continue;
            }
            fn(x, y, p);
        }
    }
}

// -------------------------------------------------------------- categories
QString colorAdjustmentsCategory()
{
    return QStringLiteral("Color Adjustments");
}

QString colorEffectsCategory()
{
    return QStringLiteral("Color Effects");
}

QString blurCategory()
{
    return QStringLiteral("Blur");
}

QString stylizeCategory()
{
    return QStringLiteral("Stylize");
}

QString noiseCategory()
{
    return QStringLiteral("Noise");
}

QString layersCategory()
{
    return QStringLiteral("Layers");
}

QString miscCategory()
{
    return QStringLiteral("Miscellaneous");
}

// -------------------------------------------------------------- geometry
void rotateFlip(Surface& s, double degrees, bool maintainSize, bool smooth)
{
    if (s.isNull())
        return;
    const int w = s.width();
    const int h = s.height();
    if (w <= 0 || h <= 0)
        return;

    degrees = std::fmod(degrees, 360.0);
    if (degrees < 0.0)
        degrees += 360.0;

    QImage src = s.toQImageConst();
    if (src.isNull())
        return;
    src.setDevicePixelRatio(1.0);

    int dw = w;
    int dh = h;
    double k = 1.0;

    // Exact quarter turns: never rescale, just swap / keep the axes.
    const double quarter = std::fmod(degrees + 1e-9, 90.0);
    const bool isQuarter = quarter < 1e-6 || std::fabs(quarter - 90.0) < 1e-6;
    if (isQuarter) {
        const int q = int(std::lround(degrees / 90.0)) & 3;
        if (q == 1 || q == 3) {
            dw = h;
            dh = w;
        } else {
            dw = w;
            dh = h;
        }
        k = 1.0;
    } else if (maintainSize) {
        // Keep the canvas size: scale the rotated content so nothing is lost.
        const double rad = qDegreesToRadians(degrees);
        const double ca = std::fabs(std::cos(rad));
        const double sa = std::fabs(std::sin(rad));
        const double rw = w * ca + h * sa;
        const double rh = w * sa + h * ca;
        k = std::min(1.0, std::min(double(w) / std::max(1e-6, rw), double(h) / std::max(1e-6, rh)));
        dw = w;
        dh = h;
    } else {
        const double rad = qDegreesToRadians(degrees);
        const double ca = std::fabs(std::cos(rad));
        const double sa = std::fabs(std::sin(rad));
        dw = std::max(1, int(std::ceil(w * ca + h * sa - 0.5)));
        dh = std::max(1, int(std::ceil(w * sa + h * ca - 0.5)));
        k = 1.0;
    }

    QTransform t;
    t.translate(dw / 2.0, dh / 2.0);
    t.scale(k, k);
    t.rotate(degrees);
    t.translate(-w / 2.0, -h / 2.0);

    QImage dst(dw, dh, QImage::Format_ARGB32_Premultiplied);
    dst.setDevicePixelRatio(s.devicePixelRatio());
    dst.fill(0);
    {
        QPainter p(&dst);
        p.setRenderHint(QPainter::SmoothPixmapTransform, smooth && k != 1.0);
        p.setRenderHint(QPainter::Antialiasing, false);
        // Source replaces the (transparent) destination: correct for premultiplied data.
        p.setCompositionMode(QPainter::CompositionMode_Source);
        p.drawImage(QPointF(0.0, 0.0), src, QRectF(0, 0, src.width(), src.height()));
        p.end();
    }
    s.image() = dst;
}

void pixelateAlpha(const QImage& src, int blockWidth, int blockHeight)
{
    QImage& img = const_cast<QImage&>(src);
    if (img.isNull() || img.depth() < 8)
        return;
    const int w = img.width();
    const int h = img.height();
    if (w <= 0 || h <= 0)
        return;
    const int bw = qBound(1, blockWidth, w);
    const int bh = qBound(1, blockHeight, h);
    if (bw == 1 && bh == 1)
        return;

    const bool premult = (img.format() == QImage::Format_ARGB32_Premultiplied
                          || img.format() == QImage::Format_ARGB32_Premultiplied);

    for (int by = 0; by < h; by += bh) {
        const int y1 = std::min(by + bh, h);
        for (int bx = 0; bx < w; bx += bw) {
            const int x1 = std::min(bx + bw, w);
            int a = 0, r = 0, g = 0, b = 0, n = 0;
            for (int y = by; y < y1; ++y) {
                QRgb* row = reinterpret_cast<QRgb*>(img.scanLine(y));
                for (int x = bx; x < x1; ++x) {
                    const QRgb p = row[x];
                    if (premult) {
                        // qUnpremultiply() works on a whole QRgb, not on a channel.
                        const QRgb u = qUnpremultiply(p);
                        a += qAlpha(u);
                        r += qRed(u);
                        g += qGreen(u);
                        b += qBlue(u);
                    } else {
                        a += qAlpha(p);
                        r += qRed(p);
                        g += qGreen(p);
                        b += qBlue(p);
                    }
                    ++n;
                }
            }
            if (n <= 0)
                continue;
            a = (a + n / 2) / n;
            r = (r + n / 2) / n;
            g = (g + n / 2) / n;
            b = (b + n / 2) / n;
            QRgb out;
            if (premult)
                out = qPremultiply(qRgba(r, g, b, a));
            else
                out = qRgba(r, g, b, a);
            for (int y = by; y < y1; ++y) {
                QRgb* row = reinterpret_cast<QRgb*>(img.scanLine(y));
                for (int x = bx; x < x1; ++x)
                    row[x] = out;
            }
        }
    }
}

} // namespace Effects
} // namespace pnq
