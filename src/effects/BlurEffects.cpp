// ==============================================================
//  BlurEffects.cpp - blur, glow and shadow effects.
// ==============================================================
#include "effects/Effects.h"

#include "core/ColorUtils.h"
#include "core/ImageMath.h"

#include <QImage>
#include <QtMath>
#include <QVector>

#include <algorithm>
#include <cmath>

namespace pnq {
namespace Effects {
namespace {

// -------------------------------------------------------------- utils
inline int clamp255d(double v)
{
    return int(v < 0.0 ? 0.0 : (v > 255.0 ? 255.0 : (v + 0.5)));
}

// Unpremultiplied channel access.
// NOTE: pnq::getR()/getG()/getB() (core/ColorUtils.h) are broken with Qt 6:
// qUnpremultiply() takes a full QRgb there, so they always return 0. These
// local helpers are the corrected equivalents and can be dropped once the
// core header is fixed.
inline int unpremulCh(int c, int a)
{
    if (a <= 0)
        return 0;
    if (a >= 255)
        return c;
    return (c * 255 + a / 2) / a;
}
inline int uR(pixel_t p) { return unpremulCh(chanR(p), getA(p)); }
inline int uG(pixel_t p) { return unpremulCh(chanG(p), getA(p)); }
inline int uB(pixel_t p) { return unpremulCh(chanB(p), getA(p)); }

/// Per channel interpolation of two premultiplied pixels (k = weight of `s`).
/// NOTE: the packed value must never be multiplied/scaled as a whole, the
/// carries would leak from one channel into the next one.
inline pixel_t mixPremult(pixel_t d, pixel_t s, int k)
{
    if (k <= 0)
        return d;
    if (k >= 255)
        return s;
    const int ik = 255 - k;
    const quint32 a = quint32((int(chanA(s)) * k + int(chanA(d)) * ik + 127) / 255);
    const quint32 r = std::min(a, quint32((int(chanR(s)) * k + int(chanR(d)) * ik + 127) / 255));
    const quint32 g = std::min(a, quint32((int(chanG(s)) * k + int(chanG(d)) * ik + 127) / 255));
    const quint32 b = std::min(a, quint32((int(chanB(s)) * k + int(chanB(d)) * ik + 127) / 255));
    return (a << 24) | (r << 16) | (g << 8) | b;
}

/// Scales a premultiplied pixel by k/255 (per channel).
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

/// Source-over compositing of two premultiplied pixels.
/// NOTE: pnq::composePixel() cannot be used, it relies on the broken
/// pnq::getR()/getG()/getB() and therefore always returns black.
inline pixel_t composeLocal(pixel_t dst, pixel_t src, BlendMode mode, int opacity = 255)
{
    int sa = getA(src);
    if (opacity != 255)
        sa = sa * opacity / 255;
    if (sa <= 0)
        return dst;
    const int da = getA(dst);
    const int br = uR(dst), bg = uG(dst), bb = uB(dst);
    const int sr = uR(src), sg = uG(src), sb = uB(src);
    if (mode == BlendMode::Normal || mode == BlendMode::LegacyNormal) {
        if (da == 0)
            return qPremult(quint8(sa), quint8(sr), quint8(sg), quint8(sb));
        const int k1 = sa * (255 - da) / 255;
        const int k2 = sa * da / 255;
        const int k3 = (255 - sa) * da / 255;
        const auto ch = [&](int s, int d, int f) { return quint8((k1 * s + k2 * f + k3 * d) / 255); };
        return qPremult(quint8(sa + da * (255 - sa) / 255), ch(sr, br, sr), ch(sg, bg, sg), ch(sb, bb, sb));
    }
    if (da == 0)
        return qPremult(quint8(sa), quint8(sr), quint8(sg), quint8(sb));
    const double as = sa / 255.0, ab = da / 255.0;
    int fr = 0, fg = 0, fb = 0;
    switch (mode) {
    case BlendMode::DarkerColor: {
        const int ds = br + bg + bb, ss = sr + sg + sb;
        fr = (ss <= ds) ? sr : br;
        fg = (ss <= ds) ? sg : bg;
        fb = (ss <= ds) ? sb : bb;
        break;
    }
    case BlendMode::LighterColor: {
        const int ds = br + bg + bb, ss = sr + sg + sb;
        fr = (ss >= ds) ? sr : br;
        fg = (ss >= ds) ? sg : bg;
        fb = (ss >= ds) ? sb : bb;
        break;
    }
    case BlendMode::Hue:
    case BlendMode::Saturation:
    case BlendMode::Color:
    case BlendMode::Luminosity: {
        int orr = 0, org = 0, orb = 0;
        blendNonSeparable(mode, br, bg, bb, sr, sg, sb, &orr, &org, &orb);
        fr = orr;
        fg = org;
        fb = orb;
        break;
    }
    default:
        fr = blendChannel(mode, br, sr);
        fg = blendChannel(mode, bg, sg);
        fb = blendChannel(mode, bb, sb);
        break;
    }
    const double k1 = as * (1.0 - ab);
    const double k2 = as * ab;
    const double k3 = (1.0 - as) * ab;
    return qPremult(quint8(sa + da * (255 - sa) / 255), quint8(clamp255d(k1 * sr + k2 * fr + k3 * br)),
                    quint8(clamp255d(k1 * sg + k2 * fg + k3 * bg)),
                    quint8(clamp255d(k1 * sb + k2 * fb + k3 * bb)));
}

inline int luma255(int r, int g, int b)
{
    return clamp255d(0.299 * r + 0.587 * g + 0.114 * b);
}

/// Clamps an accumulator to the 0..255 channel range.
inline quint32 acc255(double v)
{
    return quint32(v < 0.0 ? 0.0 : (v > 255.0 ? 255.0 : v + 0.5));
}

/// Fast selection sampling (one scan line lookup per row).
struct SelReader
{
    const QImage* mask = nullptr;

    SelReader(const Selection& sel, int w, int h)
    {
        if (!sel.isNull() && sel.width() == w && sel.height() == h) {
            const QImage& m = sel.mask();
            if (!m.isNull())
                mask = &m;
        }
    }

    inline quint8 at(int x, int y) const
    {
        return mask ? quint8(mask->constScanLine(y)[x]) : quint8(255);
    }
};

inline quint8 alphaAt(const QImage& a, int x, int y)
{
    if (a.isNull())
        return 0;
    x = qBound(0, x, a.width() - 1);
    y = qBound(0, y, a.height() - 1);
    return quint8(a.constScanLine(y)[x]);
}

/// Extracts the alpha channel of a surface as a Format_Alpha8 image.
QImage extractAlpha(const Surface& s)
{
    const int w = s.width(), h = s.height();
    QImage a(w, h, QImage::Format_Alpha8);
    if (w <= 0 || h <= 0) {
        a.fill(0);
        return a;
    }
    for (int y = 0; y < h; ++y) {
        const pixel_t* row = s.scanLine(y);
        uchar* d = a.scanLine(y);
        for (int x = 0; x < w; ++x)
            d[x] = uchar(getA(row[x]));
    }
    return a;
}

void makeMonochrome(Surface& surf)
{
    for (int y = 0; y < surf.height(); ++y) {
        pixel_t* row = surf.scanLine(y);
        for (int x = 0; x < surf.width(); ++x) {
            const quint8 a = getA(row[x]);
            if (a == 0)
                continue;
            const quint8 l = quint8(luma255(uR(row[x]), uG(row[x]), uB(row[x])));
            row[x] = qPremult(a, l, l, l);
        }
    }
}

/// Bilinear sample of a premultiplied surface with clamped edges.
pixel_t sampleBilinear(const Surface& src, double x, double y)
{
    const int w = src.width(), h = src.height();
    if (w <= 0 || h <= 0)
        return 0;
    x = qBound(0.0, x, double(w - 1));
    y = qBound(0.0, y, double(h - 1));
    const int x0 = int(x), y0 = int(y);
    const int x1 = std::min(x0 + 1, w - 1);
    const int y1 = std::min(y0 + 1, h - 1);
    const double fx = x - x0, fy = y - y0;
    const pixel_t* r0 = src.scanLine(y0);
    const pixel_t* r1 = src.scanLine(y1);
    const pixel_t p00 = r0[x0], p10 = r0[x1], p01 = r1[x0], p11 = r1[x1];
    const auto mixc = [&](int c00, int c10, int c01, int c11) {
        const double a = (c00 + (c10 - c00) * fx) * (1.0 - fy) + (c01 + (c11 - c01) * fx) * fy;
        return int(a < 0 ? 0 : (a > 255 ? 255 : (a + 0.5)));
    };
    const quint32 a = quint32(mixc(chanA(p00), chanA(p10), chanA(p01), chanA(p11)));
    quint32 r = quint32(mixc(chanR(p00), chanR(p10), chanR(p01), chanR(p11)));
    quint32 g = quint32(mixc(chanG(p00), chanG(p10), chanG(p01), chanG(p11)));
    quint32 b = quint32(mixc(chanB(p00), chanB(p10), chanB(p01), chanB(p11)));
    r = std::min(r, a);
    g = std::min(g, a);
    b = std::min(b, a);
    return (a << 24) | (r << 16) | (g << 8) | b;
}

/// Composites `src` over the surface, limited to the selection coverage.
void composeResult(Surface& s, const Selection& sel, const Surface& src, BlendMode mode = BlendMode::Normal)
{
    const int w = s.width(), h = s.height();
    if (w <= 0 || h <= 0)
        return;
    const SelReader sr(sel, w, h);
    for (int y = 0; y < h; ++y) {
        pixel_t* d = s.scanLine(y);
        if (y >= src.height())
            continue;
        const pixel_t* r = src.scanLine(y);
        for (int x = 0; x < w; ++x) {
            const quint8 cov = sr.at(x, y);
            if (cov == 0)
                continue;
            d[x] = composeLocal(d[x], r[x], mode, cov);
        }
    }
}

/// Replaces the pixels with `src`, honouring partial selection coverage.
void replaceResult(Surface& s, const Selection& sel, const Surface& src)
{
    const int w = s.width(), h = s.height();
    if (w <= 0 || h <= 0)
        return;
    const SelReader sr(sel, w, h);
    for (int y = 0; y < h; ++y) {
        pixel_t* d = s.scanLine(y);
        if (y >= src.height())
            continue;
        const pixel_t* r = src.scanLine(y);
        for (int x = 0; x < w; ++x) {
            const quint8 cov = sr.at(x, y);
            if (cov == 0)
                continue;
            d[x] = mixPremult(d[x], r[x], cov);
        }
    }
}

/// Separable box blur on premultiplied channels (running sum, O(n)).
void boxBlurPremult(Surface& surf, int radius)
{
    if (radius < 1 || surf.isNull())
        return;
    const int w = surf.width(), h = surf.height();
    if (w <= 0 || h <= 0)
        return;
    const int r = std::min(radius, std::max(w, h));
    std::vector<pixel_t> tmp(size_t(w) * size_t(h));
    for (int y = 0; y < h; ++y) {
        const pixel_t* s = surf.scanLine(y);
        long long a = 0, cr = 0, cg = 0, cb = 0;
        for (int i = -r; i <= r; ++i) {
            const pixel_t p = s[qBound(0, i, w - 1)];
            a += chanA(p);
            cr += chanR(p);
            cg += chanG(p);
            cb += chanB(p);
        }
        const int n = 2 * r + 1;
        for (int x = 0; x < w; ++x) {
            pixel_t* d = &tmp[size_t(y) * w + x];
            *d = (acc255(a / n) << 24) | (acc255(cr / n) << 16)
                 | (acc255(cg / n) << 8) | acc255(cb / n);
            const pixel_t pin = s[qBound(0, x + r + 1, w - 1)];
            const pixel_t pout = s[qBound(0, x - r, w - 1)];
            a += chanA(pin) - chanA(pout);
            cr += chanR(pin) - chanR(pout);
            cg += chanG(pin) - chanG(pout);
            cb += chanB(pin) - chanB(pout);
        }
    }
    for (int x = 0; x < w; ++x) {
        long long a = 0, cr = 0, cg = 0, cb = 0;
        for (int i = -r; i <= r; ++i) {
            const pixel_t p = tmp[size_t(qBound(0, i, h - 1)) * w + x];
            a += chanA(p);
            cr += chanR(p);
            cg += chanG(p);
            cb += chanB(p);
        }
        const int n = 2 * r + 1;
        for (int y = 0; y < h; ++y) {
            const quint32 av = acc255(a / n);
            const quint32 rv = std::min(av, acc255(cr / n));
            const quint32 gv = std::min(av, acc255(cg / n));
            const quint32 bv = std::min(av, acc255(cb / n));
            surf.scanLine(y)[x] = (av << 24) | (rv << 16) | (gv << 8) | bv;
            const pixel_t pin = tmp[size_t(qBound(0, y + r + 1, h - 1)) * w + x];
            const pixel_t pout = tmp[size_t(qBound(0, y - r, h - 1)) * w + x];
            a += chanA(pin) - chanA(pout);
            cr += chanR(pin) - chanR(pout);
            cg += chanG(pin) - chanG(pout);
            cb += chanB(pin) - chanB(pout);
        }
    }
}

/// Gaussian blur of a premultiplied surface (colour + alpha).
void gaussianBlurSurface(Surface& work, double radius)
{
    if (radius < 0.1)
        return;
    ImageMath::convolve(work, ImageMath::gaussianKernel(float(radius)));
    QImage a = extractAlpha(work);
    ImageMath::gaussianBlurAlpha(a, float(radius));
    const int w = work.width(), h = work.height();
    for (int y = 0; y < h; ++y) {
        pixel_t* row = work.scanLine(y);
        const uchar* ar = a.constScanLine(y);
        for (int x = 0; x < w; ++x) {
            const quint8 na = ar[x];
            if (na == 0) {
                row[x] = 0;
                continue;
            }
            const pixel_t p = row[x];
            const int r = std::min(int(chanR(p)), int(na));
            const int g = std::min(int(chanG(p)), int(na));
            const int b = std::min(int(chanB(p)), int(na));
            row[x] = (quint32(na) << 24) | (quint32(r) << 16) | (quint32(g) << 8) | quint32(b);
        }
    }
}

/// Alpha image translated by (dx, dy), edges clamped to zero.
QImage shiftAlpha(const QImage& a, int dx, int dy)
{
    QImage out(a.size(), QImage::Format_Alpha8);
    out.fill(0);
    if (a.isNull())
        return out;
    const int w = a.width(), h = a.height();
    for (int y = 0; y < h; ++y) {
        const int sy = y - dy;
        if (sy < 0 || sy >= h)
            continue;
        const uchar* s = a.constScanLine(sy);
        uchar* d = out.scanLine(y);
        for (int x = 0; x < w; ++x) {
            const int sx = x - dx;
            if (sx < 0 || sx >= w)
                continue;
            d[x] = s[sx];
        }
    }
    return out;
}

/// Additive (screen like) composition of two premultiplied pixels.
inline pixel_t addPixels(pixel_t d, pixel_t g)
{
    quint32 a = std::min(255u, quint32(chanA(d)) + quint32(chanA(g)));
    quint32 r = std::min(a, quint32(chanR(d)) + quint32(chanR(g)));
    quint32 gg = std::min(a, quint32(chanG(d)) + quint32(chanG(g)));
    quint32 b = std::min(a, quint32(chanB(d)) + quint32(chanB(g)));
    return (a << 24) | (r << 16) | (gg << 8) | b;
}

} // namespace

// ==============================================================
//  Gaussian blur
// ==============================================================
void blurGaussian(Surface& s, const Selection& sel, double radius, bool monochrome, bool deepAnalysis)
{
    if (s.isNull() || radius < 0.1)
        return;
    Surface work = s.copy();
    if (monochrome)
        makeMonochrome(work);
    gaussianBlurSurface(work, radius);

    if (deepAnalysis) {
        // Re-apply the original coverage so that semi transparent halos and
        // colour bleeding from fully transparent pixels are removed.
        const int w = work.width(), h = work.height();
        for (int y = 0; y < h; ++y) {
            const pixel_t* orig = s.scanLine(y);
            pixel_t* row = work.scanLine(y);
            for (int x = 0; x < w; ++x) {
                const quint8 oa = getA(orig[x]);
                if (oa == 0) {
                    row[x] = 0;
                    continue;
                }
                const pixel_t p = row[x];
                const int na = std::min(int(oa), int(chanA(p)));
                if (na <= 0) {
                    row[x] = 0;
                    continue;
                }
                const int r = qBound(0, (chanR(p) * 255) / na, 255);
                const int g = qBound(0, (chanG(p) * 255) / na, 255);
                const int b = qBound(0, (chanB(p) * 255) / na, 255);
                // Keep the original coverage but keep the blurred colour.
                row[x] = qPremult(oa, quint8(std::min(r, int(oa))), quint8(std::min(g, int(oa))),
                                  quint8(std::min(b, int(oa))));
            }
        }
    }
    replaceResult(s, sel, work);
}

// ==============================================================
//  Box blur
// ==============================================================
void blurBox(Surface& s, const Selection& sel, double radius, bool monochrome)
{
    if (s.isNull() || radius < 1.0)
        return;
    Surface work = s.copy();
    if (monochrome)
        makeMonochrome(work);
    const int r = std::min(int(std::lround(radius)), std::max(work.width(), work.height()));
    boxBlurPremult(work, r);
    replaceResult(s, sel, work);
}

// ==============================================================
//  Motion blur
// ==============================================================
void blurMotion(Surface& s, const Selection& sel, double angle, double sampleCount)
{
    if (s.isNull())
        return;
    const Surface src = s.copy();
    const int w = src.width(), h = src.height();
    if (w <= 0 || h <= 0)
        return;
    const int samples = qBound(2, int(std::lround(sampleCount)), 512);
    const double rad = qDegreesToRadians(angle);
    const double dx = std::cos(rad);
    const double dy = std::sin(rad);
    // Spread the samples over a distance proportional to the image size.
    const double dist = std::max(2.0, std::min(std::max(w, h) * 0.25, samples * 0.6));

    const SelReader sr(sel, w, h);
    for (int y = 0; y < h; ++y) {
        pixel_t* d = s.scanLine(y);
        for (int x = 0; x < w; ++x) {
            const quint8 cov = sr.at(x, y);
            if (cov == 0)
                continue;
            double a = 0.0, r = 0.0, g = 0.0, b = 0.0;
            for (int i = 0; i < samples; ++i) {
                const double t = (double(i) / double(samples - 1) - 0.5) * dist;
                const pixel_t p = sampleBilinear(src, x + dx * t, y + dy * t);
                a += chanA(p);
                r += chanR(p);
                g += chanG(p);
                b += chanB(p);
            }
            const double n = double(samples);
            const quint32 av = acc255(a / n);
            const quint32 rv = std::min(av, acc255(r / n));
            const quint32 gv = std::min(av, acc255(g / n));
            const quint32 bv = std::min(av, acc255(b / n));
            const pixel_t res = (av << 24) | (rv << 16) | (gv << 8) | bv;
            d[x] = mixPremult(d[x], res, cov);
        }
    }
}

// ==============================================================
//  Zoom blur
// ==============================================================
void blurZoom(Surface& s, const Selection& sel, int cx, int cy, int amount)
{
    if (s.isNull())
        return;
    const int amt = qBound(0, amount, 100);
    if (amt == 0)
        return;
    const Surface src = s.copy();
    const int w = src.width(), h = src.height();
    if (w <= 0 || h <= 0)
        return;
    const double fcx = cx < 0 ? w / 2.0 : std::min(double(cx), double(w - 1));
    const double fcy = cy < 0 ? h / 2.0 : std::min(double(cy), double(h - 1));
    const int samples = qBound(8, amt * 2, 120);
    const double zoom = 1.0 + double(amt) / 100.0 * 0.9;

    const SelReader sr(sel, w, h);
    for (int y = 0; y < h; ++y) {
        pixel_t* d = s.scanLine(y);
        for (int x = 0; x < w; ++x) {
            const quint8 cov = sr.at(x, y);
            if (cov == 0)
                continue;
            double a = 0.0, r = 0.0, g = 0.0, b = 0.0;
            for (int i = 0; i < samples; ++i) {
                const double t = double(i) / double(samples - 1);
                const double k = 1.0 + (zoom - 1.0) * t;
                const pixel_t p = sampleBilinear(src, fcx + (x - fcx) * k, fcy + (y - fcy) * k);
                a += chanA(p);
                r += chanR(p);
                g += chanG(p);
                b += chanB(p);
            }
            const double n = double(samples);
            const quint32 av = acc255(a / n);
            const quint32 rv = std::min(av, acc255(r / n));
            const quint32 gv = std::min(av, acc255(g / n));
            const quint32 bv = std::min(av, acc255(b / n));
            const pixel_t res = (av << 24) | (rv << 16) | (gv << 8) | bv;
            d[x] = mixPremult(d[x], res, cov);
        }
    }
}

// ==============================================================
//  Radial (circular) blur
// ==============================================================
void blurRadial(Surface& s, const Selection& sel, int cx, int cy, int amount)
{
    if (s.isNull())
        return;
    const int amt = qBound(0, amount, 100);
    if (amt == 0)
        return;
    const Surface src = s.copy();
    const int w = src.width(), h = src.height();
    if (w <= 0 || h <= 0)
        return;
    const double fcx = cx < 0 ? w / 2.0 : std::min(double(cx), double(w - 1));
    const double fcy = cy < 0 ? h / 2.0 : std::min(double(cy), double(h - 1));
    const int samples = 36;
    const double totalAngle = 2.0 * M_PI * (double(amt) / 100.0);

    const SelReader sr(sel, w, h);
    for (int y = 0; y < h; ++y) {
        pixel_t* d = s.scanLine(y);
        for (int x = 0; x < w; ++x) {
            const quint8 cov = sr.at(x, y);
            if (cov == 0)
                continue;
            const double ox = x - fcx, oy = y - fcy;
            double a = 0.0, r = 0.0, g = 0.0, b = 0.0;
            for (int i = 0; i < samples; ++i) {
                const double ang = totalAngle * double(i) / double(samples);
                const double ca = std::cos(ang), sa = std::sin(ang);
                const pixel_t p = sampleBilinear(src, fcx + ox * ca - oy * sa, fcy + ox * sa + oy * ca);
                a += chanA(p);
                r += chanR(p);
                g += chanG(p);
                b += chanB(p);
            }
            const double n = double(samples);
            const quint32 av = acc255(a / n);
            const quint32 rv = std::min(av, acc255(r / n));
            const quint32 gv = std::min(av, acc255(g / n));
            const quint32 bv = std::min(av, acc255(b / n));
            const pixel_t res = (av << 24) | (rv << 16) | (gv << 8) | bv;
            d[x] = mixPremult(d[x], res, cov);
        }
    }
}

// ==============================================================
//  Surface blur (bilateral)
// ==============================================================
void blurSurface(Surface& s, const Selection& sel, double strength, double colorStrength, double size,
                 bool monochrome, int seed)
{
    if (s.isNull())
        return;
    const int r = qBound(1, int(std::lround(size)), 48);
    const double f = qBound(0.0, strength / 100.0, 1.0);
    if (f <= 0.0)
        return;
    const double sigmaC = std::max(1.0, colorStrength * 2.55); // 0..100 -> 1..255
    const Surface src = s.copy();
    Surface work;
    if (monochrome) {
        work = s.copy();
        makeMonochrome(work);
    }
    const Surface& from = monochrome ? work : src;
    const int w = from.width(), h = from.height();
    if (w <= 0 || h <= 0)
        return;

    const int step = std::max(1, r / 5);
    const double sigmaP = std::max(1.0, double(r) / 2.0);
    const double inv2p = 1.0 / (2.0 * sigmaP * sigmaP);
    const double inv2c = 1.0 / (2.0 * sigmaC * sigmaC);
    quint32 rng = quint32(seed) * 2654435761u + 0x9E3779B9u;

    const SelReader sr(sel, w, h);
    for (int y = 0; y < h; ++y) {
        pixel_t* d = s.scanLine(y);
        for (int x = 0; x < w; ++x) {
            const quint8 cov = sr.at(x, y);
            if (cov == 0)
                continue;
            const pixel_t centre = from.scanLine(y)[x];
            const int cr = uR(centre), cg = uG(centre), cb = uB(centre);
            double a = 0.0, rr = 0.0, gg = 0.0, bb = 0.0, wsum = 0.0;
            // Deterministic jitter of the sampling grid.
            rng = rng * 1664525u + 1013904223u;
            const int jx = int((rng >> 8) % 3) - 1;
            for (int dy = -r; dy <= r; dy += step) {
                const int yy = y + dy;
                if (yy < 0 || yy >= h)
                    continue;
                const pixel_t* row = from.scanLine(yy);
                for (int dx = -r; dx <= r; dx += step) {
                    const int xx = x + dx + jx;
                    if (xx < 0 || xx >= w)
                        continue;
                    const pixel_t p = row[xx];
                    const double dr = double(uR(p)) - cr;
                    const double dg = double(uG(p)) - cg;
                    const double db = double(uB(p)) - cb;
                    const double cd = dr * dr + dg * dg + db * db;
                    const double dd = double(dx * dx + dy * dy);
                    const double wt = std::exp(-dd * inv2p) * std::exp(-cd * inv2c);
                    if (wt < 1e-4)
                        continue;
                    wsum += wt;
                    a += chanA(p) * wt;
                    rr += chanR(p) * wt;
                    gg += chanG(p) * wt;
                    bb += chanB(p) * wt;
                }
            }
            if (wsum <= 1e-6)
                continue;
            const quint32 av = acc255(a / wsum);
            const quint32 rv = std::min(av, acc255(rr / wsum));
            const quint32 gv = std::min(av, acc255(gg / wsum));
            const quint32 bv = std::min(av, acc255(bb / wsum));
            const pixel_t res = (av << 24) | (rv << 16) | (gv << 8) | bv;
            const int mixk = int(std::lround(f * 255.0));
            d[x] = mixPremult(d[x], res, mixk);
        }
    }
}

// ==============================================================
//  Glow
// ==============================================================
void glow(Surface& s, const Selection& sel, int radius, int intensity, pixel_t glowColor, bool centerAura)
{
    if (s.isNull())
        return;
    const int rad = qBound(0, radius, 500);
    const double inten = qBound(0.0, double(intensity) / 100.0, 2.0);
    if (rad == 0 && !centerAura)
        return;
    const quint8 gr = uR(glowColor), gg = uG(glowColor), gb = uB(glowColor);

    QImage a = extractAlpha(s);
    if (rad > 0)
        ImageMath::gaussianBlurAlpha(a, float(rad));

    const int w = s.width(), h = s.height();
    if (w <= 0 || h <= 0)
        return;
    const double maxd = std::sqrt(double(w) * w + double(h) * h) * 0.5;
    const SelReader sr(sel, w, h);

    for (int y = 0; y < h; ++y) {
        pixel_t* row = s.scanLine(y);
        const uchar* ar = a.constScanLine(y);
        for (int x = 0; x < w; ++x) {
            const quint8 cov = sr.at(x, y);
            if (cov == 0)
                continue;
            double ga = double(ar[x]) * inten / 255.0;
            if (centerAura) {
                const double dx = x - w * 0.5, dy = y - h * 0.5;
                const double dd = std::sqrt(dx * dx + dy * dy) / std::max(1.0, maxd);
                const double aura = std::pow(std::max(0.0, 1.0 - dd), 2.0) * inten * 0.75;
                ga = std::max(ga, aura);
            }
            if (ga <= 0.5)
                continue;
            const quint8 ga8 = quint8(qBound(0.0, ga, 255.0));
            pixel_t gpx = qPremult(ga8, gr, gg, gb);
            if (cov < 255) {
                gpx = scalePremult(gpx, cov);
            }
            row[x] = addPixels(row[x], gpx);
        }
    }
}

// ==============================================================
//  Shadow (drawn under the current content)
// ==============================================================
void shadow(Surface& s, const Selection& sel, int blurRadius, int offsetX, int offsetY, pixel_t shadowColor,
            int opacity)
{
    if (s.isNull())
        return;
    const int rad = qBound(0, blurRadius, 500);
    const double op = qBound(0.0, double(opacity) / 100.0, 1.0);
    if (op <= 0.0)
        return;
    const quint8 cr = uR(shadowColor), cg = uG(shadowColor), cb = uB(shadowColor);

    QImage a = extractAlpha(s);
    a = shiftAlpha(a, offsetX, offsetY);
    if (rad > 0)
        ImageMath::gaussianBlurAlpha(a, float(rad));

    const int w = s.width(), h = s.height();
    if (w <= 0 || h <= 0)
        return;
    const SelReader sr(sel, w, h);
    for (int y = 0; y < h; ++y) {
        pixel_t* row = s.scanLine(y);
        const uchar* ar = a.constScanLine(y);
        for (int x = 0; x < w; ++x) {
            const quint8 cov = sr.at(x, y);
            if (cov == 0)
                continue;
            const quint8 sa = quint8(double(ar[x]) * op);
            if (sa == 0)
                continue;
            const pixel_t sh = qPremult(sa, cr, cg, cb);
            row[x] = composeLocal(sh, row[x], BlendMode::Normal, cov);
        }
    }
}

// ==============================================================
//  Drop shadow
// ==============================================================
void dropShadow(Surface& s, const Selection& sel, int blurRadius, int offsetX, int offsetY, pixel_t shadowColor,
                int opacity)
{
    if (s.isNull())
        return;
    const int rad = qBound(0, blurRadius, 500);
    const double op = qBound(0.0, double(opacity) / 100.0, 1.0);
    if (op <= 0.0)
        return;
    const quint8 cr = uR(shadowColor), cg = uG(shadowColor), cb = uB(shadowColor);

    QImage a = extractAlpha(s);
    a = shiftAlpha(a, offsetX, offsetY);
    if (rad > 0)
        ImageMath::gaussianBlurAlpha(a, float(rad));

    const int w = s.width(), h = s.height();
    if (w <= 0 || h <= 0)
        return;
    const SelReader sr(sel, w, h);
    for (int y = 0; y < h; ++y) {
        pixel_t* row = s.scanLine(y);
        const uchar* ar = a.constScanLine(y);
        for (int x = 0; x < w; ++x) {
            const quint8 cov = sr.at(x, y);
            if (cov == 0)
                continue;
            const quint8 sa = quint8(double(ar[x]) * op);
            if (sa != 0) {
                const pixel_t sh = qPremult(sa, cr, cg, cb);
                // The shadow multiplies into what is behind the object.
                pixel_t under = composeLocal(row[x], sh, BlendMode::Multiply);
                row[x] = composeLocal(under, row[x], BlendMode::Normal);
            }
        }
    }
}

// ==============================================================
//  Inner shadow
// ==============================================================
void innerShadow(Surface& s, const Selection& sel, int blurRadius, int offsetX, int offsetY, pixel_t shadowColor,
                 int opacity, bool invertSelection)
{
    if (s.isNull())
        return;
    const int rad = qBound(0, blurRadius, 500);
    const double op = qBound(0.0, double(opacity) / 100.0, 1.0);
    if (op <= 0.0)
        return;
    const quint8 cr = uR(shadowColor), cg = uG(shadowColor), cb = uB(shadowColor);

    const QImage orig = extractAlpha(s);
    QImage off = shiftAlpha(orig, offsetX, offsetY);
    if (rad > 0)
        ImageMath::gaussianBlurAlpha(off, float(rad));

    const int w = s.width(), h = s.height();
    if (w <= 0 || h <= 0)
        return;
    const SelReader sr(sel, w, h);
    for (int y = 0; y < h; ++y) {
        pixel_t* row = s.scanLine(y);
        const uchar* orow = orig.constScanLine(y);
        const uchar* frow = off.constScanLine(y);
        for (int x = 0; x < w; ++x) {
            const quint8 cov = sr.at(x, y);
            if (cov == 0)
                continue;
            int m = invertSelection ? int(frow[x]) : (255 - int(frow[x]));
            m = (m * int(orow[x])) / 255;
            m = int(m * op);
            if (m <= 0)
                continue;
            const pixel_t sh = qPremult(quint8(qBound(0, m, 255)), cr, cg, cb);
            row[x] = composeLocal(row[x], sh, BlendMode::Multiply, cov);
        }
    }
}

} // namespace Effects
} // namespace pnq
