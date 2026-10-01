#include "core/ImageMath.h"
#include "core/ColorUtils.h"
#include "core/Selection.h"

#include <QPainter>
#include <QPainterPath>
#include <QStack>
#include <QtMath>
#include <algorithm>
#include <cmath>

namespace pnq {
namespace ImageMath {

QRect clampRect(const QRect& r, const QSize& size)
{
    QRect out = r;
    if (out.left() < 0) {
        out.setLeft(0);
        out.setWidth(out.width() + out.left());
    }
    if (out.top() < 0) {
        out.setTop(0);
        out.setHeight(out.height() + out.top());
    }
    if (out.right() > size.width() - 1)
        out.setWidth(size.width() - out.left());
    if (out.bottom() > size.height() - 1)
        out.setHeight(size.height() - out.top());
    if (out.width() < 0 || out.height() < 0)
        return QRect();
    return out;
}

QRect lineRect(const QPoint& p0, const QPoint& p1)
{
    return QRect(QPoint(qMin(p0.x(), p1.x()), qMin(p0.y(), p1.y())),
                 QPoint(qMax(p0.x(), p1.x()), qMax(p0.y(), p1.y())))
        .normalized();
}

QVector<float> boxKernel(int radius)
{
    QVector<float> k(radius * 2 + 1, 1.0f);
    return k;
}

QVector<float> triangleKernel(float radius)
{
    int r = qMax(1, int(radius));
    QVector<float> k;
    k.reserve(r * 2 + 1);
    float sum = 0;
    for (int i = -r; i <= r; ++i) {
        float v = r - std::abs(i) + 1.0f;
        k.append(v);
        sum += v;
    }
    for (float& v : k)
        v /= sum;
    return k;
}

QVector<float> gaussianKernel(float radius)
{
    if (radius < 0.5f)
        return { 1.0f };
    int r = int(std::ceil(radius));
    QVector<float> k(2 * r + 1);
    const float sigma = radius / 2.0f > 0 ? radius / 2.0f : 0.5f;
    const float twoSigmaSq = 2.0f * sigma * sigma;
    float sum = 0;
    for (int i = -r; i <= r; ++i) {
        float v = std::exp(-float(i * i) / twoSigmaSq);
        k[i + r] = v;
        sum += v;
    }
    for (float& v : k)
        v /= sum;
    return k;
}

static void convolveAlphaChannel(const QImage& src, QImage& dst, const QVector<float>& k)
{
    const int w = src.width(), h = src.height();
    const int r = (k.size() - 1) / 2;
    QImage tmp(w, h, QImage::Format_Alpha8);
    tmp.fill(0);
    for (int y = 0; y < h; ++y) {
        const uchar* s = src.constScanLine(y);
        uchar* d = tmp.scanLine(y);
        for (int x = 0; x < w; ++x) {
            float acc = 0;
            for (int i = -r; i <= r; ++i) {
                int xx = qBound(0, x + i, w - 1);
                acc += s[xx] * k[i + r];
            }
            d[x] = uchar(qBound(0, qRound(acc), 255));
        }
    }
    for (int y = 0; y < h; ++y) {
        uchar* d = dst.scanLine(y);
        for (int x = 0; x < w; ++x) {
            float acc = 0;
            for (int i = -r; i <= r; ++i) {
                int yy = qBound(0, y + i, h - 1);
                acc += tmp.constScanLine(yy)[x] * k[i + r];
            }
            d[x] = uchar(qBound(0, qRound(acc), 255));
        }
    }
}

void convolveAlpha(QImage& alpha8, const QVector<float>& kernel)
{
    if (alpha8.isNull() || kernel.size() <= 1)
        return;
    QImage out(alpha8.size(), QImage::Format_Alpha8);
    convolveAlphaChannel(alpha8, out, kernel);
    alpha8 = out;
}

void gaussianBlurAlpha(QImage& alpha8, float radius)
{
    if (alpha8.isNull() || radius < 0.5f)
        return;
    // Paint.NET uses three box blur passes to approximate a gaussian.
    for (int i = 0; i < 3; ++i) {
        QVector<float> k = boxKernel(int(radius));
        convolveAlpha(alpha8, k);
    }
}

void dilate(QImage& alpha8, int radius, QImage* out)
{
    if (alpha8.isNull() || radius <= 0) {
        if (out)
            *out = alpha8;
        return;
    }
    const int w = alpha8.width(), h = alpha8.height();
    QImage tmp(w, h, QImage::Format_Alpha8);
    tmp.fill(0);
    for (int y = 0; y < h; ++y) {
        const uchar* s = alpha8.constScanLine(y);
        uchar* d = tmp.scanLine(y);
        for (int x = 0; x < w; ++x) {
            uchar mx = 0;
            for (int i = -radius; i <= radius; ++i) {
                int xx = qBound(0, x + i, w - 1);
                mx = std::max(mx, s[xx]);
            }
            d[x] = mx;
        }
    }
    QImage res(w, h, QImage::Format_Alpha8);
    for (int y = 0; y < h; ++y) {
        uchar* d = res.scanLine(y);
        for (int x = 0; x < w; ++x) {
            uchar mx = 0;
            for (int i = -radius; i <= radius; ++i) {
                int yy = qBound(0, y + i, h - 1);
                mx = std::max(mx, tmp.constScanLine(yy)[x]);
            }
            d[x] = mx;
        }
    }
    if (out)
        *out = res;
    else
        alpha8 = res;
}

void erode(QImage& alpha8, int radius, QImage* out)
{
    if (alpha8.isNull() || radius <= 0) {
        if (out)
            *out = alpha8;
        return;
    }
    QImage inv = alpha8;
    for (int y = 0; y < inv.height(); ++y) {
        uchar* r = inv.scanLine(y);
        for (int x = 0; x < inv.width(); ++x)
            r[x] = uchar(255 - r[x]);
    }
    dilate(inv, radius, out);
    if (out) {
        QImage res = *out;
        for (int y = 0; y < res.height(); ++y) {
            uchar* r = res.scanLine(y);
            for (int x = 0; x < res.width(); ++x)
                r[x] = uchar(255 - r[x]);
        }
    } else {
        for (int y = 0; y < inv.height(); ++y) {
            uchar* r = inv.scanLine(y);
            for (int x = 0; x < inv.width(); ++x)
                r[x] = uchar(255 - r[x]);
        }
        alpha8 = inv;
    }
}

void convolve(Surface& surf, const QVector<float>& kernel)
{
    if (surf.isNull() || kernel.size() <= 1)
        return;
    // Operate on premultiplied pixels: separable blur with clamped edges.
    const int w = surf.width(), h = surf.height();
    const int r = (kernel.size() - 1) / 2;
    QVector<pixel_t> tmp(w * h);
    for (int y = 0; y < h; ++y) {
        const pixel_t* s = surf.scanLine(y);
        for (int x = 0; x < w; ++x) {
            float ar = 0, ag = 0, ab = 0, aa = 0;
            for (int i = -r; i <= r; ++i) {
                int xx = qBound(0, x + i, w - 1);
                pixel_t p = s[xx];
                float wgt = kernel[i + r];
                aa += getA(p) * wgt;
                ar += chanR(p) * wgt;
                ag += chanG(p) * wgt;
                ab += chanB(p) * wgt;
            }
            tmp[y * w + x] = qPremult(quint8(qBound(0, qRound(aa), 255)), quint8(qRound(ar)),
                                      quint8(qRound(ag)), quint8(qRound(ab)));
        }
    }
    for (int y = 0; y < h; ++y) {
        pixel_t* d = surf.scanLine(y);
        for (int x = 0; x < w; ++x) {
            float ar = 0, ag = 0, ab = 0, aa = 0;
            for (int i = -r; i <= r; ++i) {
                int yy = qBound(0, y + i, h - 1);
                pixel_t p = tmp[yy * w + x];
                float wgt = kernel[i + r];
                aa += getA(p) * wgt;
                ar += chanR(p) * wgt;
                ag += chanG(p) * wgt;
                ab += chanB(p) * wgt;
            }
            d[x] = qPremult(quint8(qBound(0, qRound(aa), 255)), quint8(qRound(ar)), quint8(qRound(ag)),
                            quint8(qRound(ab)));
        }
    }
}

void boxBlur(Surface& surf, int radius)
{
    if (radius < 1 || surf.isNull())
        return;
    // Paint.NET's blur tool: average of a square, applied on straight alpha.
    const int w = surf.width(), h = surf.height();
    QImage img = surf.toQImage();
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            int a = 0, r = 0, g = 0, b = 0, n = 0;
            for (int dy = -radius; dy <= radius; ++dy) {
                int yy = y + dy;
                if (yy < 0 || yy >= h)
                    continue;
                const QRgb* row = reinterpret_cast<const QRgb*>(img.constScanLine(yy));
                for (int dx = -radius; dx <= radius; ++dx) {
                    int xx = x + dx;
                    if (xx < 0 || xx >= w)
                        continue;
                    QRgb p = row[xx];
                    a += qAlpha(p);
                    r += qRed(p);
                    g += qGreen(p);
                    b += qBlue(p);
                    ++n;
                }
            }
            if (n == 0)
                continue;
            ((QRgb*)img.scanLine(y))[x] = qRgba(a / n, r / n, g / n, b / n);
        }
    }
    surf = Surface::fromQImage(img);
}

void unsharpMask(Surface& surf, float radius, int amount, int threshold)
{
    if (amount == 0 || surf.isNull())
        return;
    Surface blurred = surf.copy();
    convolve(blurred, gaussianKernel(radius));
    const int w = surf.width(), h = surf.height();
    for (int y = 0; y < h; ++y) {
        pixel_t* s = surf.scanLine(y);
        const pixel_t* b = blurred.scanLine(y);
        for (int x = 0; x < w; ++x) {
            for (int c = 0; c < 3; ++c) {
                int sv = (c == 0 ? getR(s[x]) : c == 1 ? getG(s[x]) : getB(s[x]));
                int bv = (c == 0 ? getR(b[x]) : c == 1 ? getG(b[x]) : getB(b[x]));
                int diff = sv - bv;
                if (qAbs(diff) < threshold)
                    continue;
                int v = sv + diff * amount / 100;
                v = qBound(0, v, 255);
                switch (c) {
                case 0: s[x] = (s[x] & 0xFF00FFFFu) | (quint32(v) << 16); break;
                case 1: s[x] = (s[x] & 0xFFFF00FFu) | (quint32(v) << 8); break;
                default: s[x] = (s[x] & 0xFFFFFF00u) | quint32(v); break;
                }
            }
        }
    }
}

void scaleAlpha(QImage& alpha8, float factor)
{
    if (alpha8.isNull())
        return;
    for (int y = 0; y < alpha8.height(); ++y) {
        uchar* r = alpha8.scanLine(y);
        for (int x = 0; x < alpha8.width(); ++x)
            r[x] = uchar(qBound(0, qRound(r[x] * factor), 255));
    }
}

void modulate(const Selection& sel, Surface& surf, float strength)
{
    if (sel.isNull() || strength >= 1.0f)
        return;
    const int w = surf.width(), h = surf.height();
    for (int y = 0; y < h; ++y) {
        pixel_t* row = surf.scanLine(y);
        const uchar* m = sel.mask().constScanLine(y);
        for (int x = 0; x < w; ++x) {
            float f = m[x] / 255.0f * strength;
            row[x] = qPremult(quint8(getA(row[x]) * f), quint8(getR(row[x]) * f), quint8(getG(row[x]) * f),
                              quint8(getB(row[x]) * f));
        }
    }
}

int sampleMask(const QImage& mask, qreal x, qreal y, bool smooth)
{
    if (mask.isNull())
        return 255;
    const int w = mask.width(), h = mask.height();
    if (!smooth) {
        int xi = int(std::floor(x + 0.5)), yi = int(std::floor(y + 0.5));
        if (xi < 0 || yi < 0 || xi >= w || yi >= h)
            return 0;
        return mask.constScanLine(yi)[xi];
    }
    qreal fx = x - 0.5, fy = y - 0.5;
    int x0 = int(std::floor(fx)), y0 = int(std::floor(fy));
    qreal tx = fx - x0, ty = fy - y0;
    auto get = [&](int xx, int yy) {
        xx = qBound(0, xx, w - 1);
        yy = qBound(0, yy, h - 1);
        return double(mask.constScanLine(yy)[xx]);
    };
    double v = get(x0, y0) * (1 - tx) * (1 - ty) + get(x0 + 1, y0) * tx * (1 - ty)
               + get(x0, y0 + 1) * (1 - tx) * ty + get(x0 + 1, y0 + 1) * tx * ty;
    return int(qBound(0.0, double(qRound(v)), 255.0));
}

void floodFill(Surface& surf, const QPoint& seed, pixel_t color, int tolerance, int fillSelection,
               QImage* maskOut)
{
    const int w = surf.width(), h = surf.height();
    if (w <= 0 || h <= 0)
        return;
    if (seed.x() < 0 || seed.y() < 0 || seed.x() >= w || seed.y() >= h)
        return;
    if (fillSelection < 0)
        fillSelection = 255;
    const pixel_t target = surf.pixel(seed.x(), seed.y());
    if (tolerance <= 0 && target == color)
        return;
    QImage filled(w, h, QImage::Format_Alpha8);
    filled.fill(0);
    QStack<QPoint> stack;
    QVector<bool> seen(w * h, false);
    stack.push(seed);
    seen[seed.y() * w + seed.x()] = true;
    const bool useTolerance = tolerance > 0;
    while (!stack.isEmpty()) {
        QPoint p = stack.pop();
        pixel_t cur = surf.pixel(p.x(), p.y());
        if (useTolerance) {
            if (colorDistance(cur, target) > tolerance)
                continue;
        } else if (cur != target) {
            continue;
        }
        filled.scanLine(p.y())[p.x()] = 255;
        const QPoint nb[4] = { QPoint(p.x() + 1, p.y()), QPoint(p.x() - 1, p.y()),
                               QPoint(p.x(), p.y() + 1), QPoint(p.x(), p.y() - 1) };
        for (const QPoint& n : nb) {
            if (n.x() < 0 || n.y() < 0 || n.x() >= w || n.y() >= h)
                continue;
            if (seen[n.y() * w + n.x()])
                continue;
            seen[n.y() * w + n.x()] = true;
            stack.push(n);
        }
    }
    if (maskOut)
        *maskOut = filled;
    for (int y = 0; y < h; ++y) {
        pixel_t* row = surf.scanLine(y);
        const uchar* m = filled.constScanLine(y);
        for (int x = 0; x < w; ++x) {
            if (!m[x])
                continue;
            int f = m[x] * fillSelection / 255;
            if (!f)
                continue;
            row[x] = composePixel(row[x], qPremult(quint8(f), getR(color), getG(color), getB(color)),
                                  BlendMode::Normal);
        }
    }
}

void fillPath(Surface& dst, const QPainterPath& path, pixel_t color, const Selection* selection,
              double opacity)
{
    QImage tmp(dst.size(), QImage::Format_ARGB32_Premultiplied);
    tmp.fill(Qt::transparent);
    {
        QPainter p(&tmp);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(toQColor(color)));
        p.drawPath(path);
    }
    const int w = dst.width(), h = dst.height();
    for (int y = 0; y < h; ++y) {
        pixel_t* row = dst.scanLine(y);
        const pixel_t* s = reinterpret_cast<const pixel_t*>(tmp.constScanLine(y));
        const uchar* m = selection ? selection->mask().constScanLine(y) : nullptr;
        for (int x = 0; x < w; ++x) {
            pixel_t sp = s[x];
            if (getA(sp) == 0)
                continue;
            int a = getA(sp);
            if (selection) {
                int sv = m ? m[x] : 255;
                if (sv == 0)
                    continue;
                a = a * sv / 255;
            }
            if (opacity < 1.0)
                a = int(a * opacity);
            if (a <= 0)
                continue;
            row[x] = composePixel(row[x], qPremult(quint8(a), getR(color), getG(color), getB(color)),
                                  BlendMode::Normal);
        }
    }
}

void strokePath(Surface& dst, const QPainterPath& path, pixel_t color, const Selection* selection,
                double opacity, double strokeWidth, bool antialias)
{
    QImage tmp(dst.size(), QImage::Format_ARGB32_Premultiplied);
    tmp.fill(Qt::transparent);
    {
        QPainter p(&tmp);
        p.setRenderHint(QPainter::Antialiasing, antialias);
        QPen pen(QColor(toQColor(color)));
        pen.setWidthF(strokeWidth);
        pen.setJoinStyle(Qt::RoundJoin);
        pen.setCapStyle(Qt::RoundCap);
        p.setPen(pen);
        p.setBrush(Qt::NoBrush);
        p.drawPath(path);
    }
    const int w = dst.width(), h = dst.height();
    for (int y = 0; y < h; ++y) {
        pixel_t* row = dst.scanLine(y);
        const pixel_t* s = reinterpret_cast<const pixel_t*>(tmp.constScanLine(y));
        const uchar* m = selection ? selection->mask().constScanLine(y) : nullptr;
        for (int x = 0; x < w; ++x) {
            pixel_t sp = s[x];
            if (getA(sp) == 0)
                continue;
            int a = getA(sp);
            if (selection) {
                int sv = m ? m[x] : 255;
                if (sv == 0)
                    continue;
                a = a * sv / 255;
            }
            if (opacity < 1.0)
                a = int(a * opacity);
            if (a <= 0)
                continue;
            row[x] = composePixel(row[x], qPremult(quint8(a), getR(color), getG(color), getB(color)),
                                  BlendMode::Normal);
        }
    }
}

void drawSmoothLine(Surface& dst, const QPointF& p0, const QPointF& p1, pixel_t color,
                    const Selection* selection, double opacity, int width)
{
    QPainterPath path;
    path.moveTo(p0);
    path.lineTo(p1);
    if (width <= 1) {
        strokePath(dst, path, color, selection, opacity, 1.0, true);
    } else {
        strokePath(dst, path, color, selection, opacity, width, true);
    }
}

} // namespace ImageMath
} // namespace pnq
