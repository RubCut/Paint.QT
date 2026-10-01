// ==============================================================
//  StylizeEffects.cpp - stylize / texture effects.
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

inline int uG(pixel_t p) { return unpremulCh(chanG(p), getA(p)); }
inline int uB(pixel_t p) { return unpremulCh(chanB(p), getA(p)); }

inline int luma255(int r, int g, int b)
{
    return clamp255d(0.299 * r + 0.587 * g + 0.114 * b);
}

inline quint32 hash32(quint32 x)
{
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

/// Deterministic pseudo random value in 0..1 for a pixel coordinate.
inline double hash01(int x, int y, quint32 seed)
{
    const quint32 h = hash32(quint32(x) * 0x9E3779B9u ^ (quint32(y) + 0x85EBCA6Bu) * 0xC2B2AE35u ^ seed);
    return double(h >> 8) / 16777215.0;
}

/// Smooth value noise (bilinear between lattice points).
double valueNoise(double x, double y, quint32 seed)
{
    const double fx = std::floor(x), fy = std::floor(y);
    const int ix = int(fx), iy = int(fy);
    const double tx = x - fx, ty = y - fy;
    const double sx = tx * tx * (3.0 - 2.0 * tx);
    const double sy = ty * ty * (3.0 - 2.0 * ty);
    const double n00 = hash01(ix, iy, seed);
    const double n10 = hash01(ix + 1, iy, seed);
    const double n01 = hash01(ix, iy + 1, seed);
    const double n11 = hash01(ix + 1, iy + 1, seed);
    return (n00 * (1 - sx) + n10 * sx) * (1 - sy) + (n01 * (1 - sx) + n11 * sx) * sy;
}

double fbm(double x, double y, int octaves, quint32 seed, double persistence = 0.5, bool turbulence = false)
{
    double sum = 0.0, amp = 1.0, norm = 0.0, freq = 1.0;
    for (int i = 0; i < octaves; ++i) {
        double n = valueNoise(x * freq, y * freq, seed + quint32(i) * 7919u);
        if (turbulence)
            n = std::fabs(n * 2.0 - 1.0);
        sum += n * amp;
        norm += amp;
        amp *= persistence;
        freq *= 2.0;
    }
    return norm > 0 ? sum / norm : 0.0;
}

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

inline pixel_t mixCov(pixel_t dst, pixel_t res, int cov)
{
    return mixPremult(dst, res, cov);
}

/// Luma of a surface pixel, clamped at the borders.
inline double lumaAt(const Surface& src, int x, int y)
{
    const int w = src.width(), h = src.height();
    if (w <= 0 || h <= 0)
        return 0.0;
    const pixel_t p = src.scanLine(qBound(0, y, h - 1))[qBound(0, x, w - 1)];
    return 0.299 * uR(p) + 0.587 * uG(p) + 0.114 * uB(p);
}

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
    const quint32 r = std::min(a, quint32(mixc(chanR(p00), chanR(p10), chanR(p01), chanR(p11))));
    const quint32 g = std::min(a, quint32(mixc(chanG(p00), chanG(p10), chanG(p01), chanG(p11))));
    const quint32 b = std::min(a, quint32(mixc(chanB(p00), chanB(p10), chanB(p01), chanB(p11))));
    return (a << 24) | (r << 16) | (g << 8) | b;
}

QImage extractAlpha(const Surface& s)
{
    const int w = s.width(), h = s.height();
    QImage a(qMax(1, w), qMax(1, h), QImage::Format_Alpha8);
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

/// Separable box blur of a premultiplied surface (running sums).
void boxBlurPremult(Surface& surf, int radius)
{
    if (radius < 1 || surf.isNull())
        return;
    const int w = surf.width(), h = surf.height();
    if (w <= 0 || h <= 0)
        return;
    const int r = std::min(radius, std::max(w, h));
    const auto clamp8 = [](long long v) { return quint32(v < 0 ? 0 : (v > 255 ? 255 : v)); };
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
            const quint32 av = clamp8(a / n);
            tmp[size_t(y) * w + x] = (av << 24) | (std::min(av, clamp8(cr / n)) << 16)
                                     | (std::min(av, clamp8(cg / n)) << 8) | std::min(av, clamp8(cb / n));
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
            const quint32 av = clamp8(a / n);
            surf.scanLine(y)[x] = (av << 24) | (std::min(av, clamp8(cr / n)) << 16)
                                  | (std::min(av, clamp8(cg / n)) << 8) | std::min(av, clamp8(cb / n));
            const pixel_t pin = tmp[size_t(qBound(0, y + r + 1, h - 1)) * w + x];
            const pixel_t pout = tmp[size_t(qBound(0, y - r, h - 1)) * w + x];
            a += chanA(pin) - chanA(pout);
            cr += chanR(pin) - chanR(pout);
            cg += chanG(pin) - chanG(pout);
            cb += chanB(pin) - chanB(pout);
        }
    }
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

inline pixel_t addPixels(pixel_t d, pixel_t g)
{
    const quint32 a = std::min(255u, quint32(chanA(d)) + quint32(chanA(g)));
    const quint32 r = std::min(a, quint32(chanR(d)) + quint32(chanR(g)));
    const quint32 gg = std::min(a, quint32(chanG(d)) + quint32(chanG(g)));
    const quint32 b = std::min(a, quint32(chanB(d)) + quint32(chanB(g)));
    return (a << 24) | (r << 16) | (gg << 8) | b;
}

/// Scale a premultiplied pixel by a 0..1 factor.
inline pixel_t scalePix(pixel_t p, double k)
{
    const double a = chanA(p) * k;
    return pixel_t((quint32(qBound(0.0, a, 255.0)) << 24) | (quint32(qBound(0.0, chanR(p) * k, 255.0)) << 16)
                   | (quint32(qBound(0.0, chanG(p) * k, 255.0)) << 8)
                   | quint32(qBound(0.0, chanB(p) * k, 255.0)));
}

// -------------------------------------------------------------- sobel
void sobelMagnitude(const Surface& src, std::vector<quint8>& out)
{
    const int w = src.width(), h = src.height();
    out.assign(size_t(std::max(0, w)) * size_t(std::max(0, h)), 0);
    if (w <= 0 || h <= 0)
        return;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const double tl = lumaAt(src, x - 1, y - 1), t = lumaAt(src, x, y - 1), tr = lumaAt(src, x + 1, y - 1);
            const double l = lumaAt(src, x - 1, y), r = lumaAt(src, x + 1, y);
            const double bl = lumaAt(src, x - 1, y + 1), b = lumaAt(src, x, y + 1), br = lumaAt(src, x + 1, y + 1);
            const double gx = (tr + 2 * r + br) - (tl + 2 * l + bl);
            const double gy = (bl + 2 * b + br) - (tl + 2 * t + tr);
            const double m = std::sqrt(gx * gx + gy * gy) * 0.25;
            out[size_t(y) * w + x] = quint8(qBound(0.0, m, 255.0));
        }
    }
}

} // namespace

// ==============================================================
//  Emboss
// ==============================================================
void emboss(Surface& s, const Selection& sel, double depth, double azimuth, double elevation, bool monochrome)
{
    if (s.isNull())
        return;
    const int w = s.width(), h = s.height();
    if (w <= 0 || h <= 0)
        return;
    const double az = qDegreesToRadians(azimuth);
    const double el = qDegreesToRadians(qBound(-89.9, elevation, 89.9));
    // Light direction in image space.
    const double lx = std::cos(el) * std::cos(az);
    const double ly = std::cos(el) * std::sin(az);
    const double strength = qBound(-5.0, depth, 5.0) / 100.0;
    // The neighbours are read from an unmodified copy: sampling the surface
    // that is being written would feed the result back into itself.
    const Surface src = s.copy();
    const SelReader sr(sel, w, h);

    for (int y = 0; y < h; ++y) {
        pixel_t* row = s.scanLine(y);
        for (int x = 0; x < w; ++x) {
            const quint8 cov = sr.at(x, y);
            if (cov == 0)
                continue;
            const pixel_t p = row[x];
            const quint8 a = getA(p);
            if (a == 0)
                continue;
            const double gx = (lumaAt(src, x + 1, y) - lumaAt(src, x - 1, y)) * 0.5;
            const double gy = (lumaAt(src, x, y + 1) - lumaAt(src, x, y - 1)) * 0.5;
            const double d = (gx * lx + gy * ly) * strength * 255.0;
            int r, g, b;
            if (monochrome) {
                r = g = b = clamp255d(127.5 + d);
            } else {
                r = clamp255d(uR(p) + d);
                g = clamp255d(uG(p) + d);
                b = clamp255d(uB(p) + d);
            }
            row[x] = mixCov(p, qPremult(a, quint8(r), quint8(g), quint8(b)), cov);
        }
    }
}

// ==============================================================
//  Invert emboss
// ==============================================================
void invertEmboss(Surface& s, const Selection& sel, double depth, double azimuth, double elevation)
{
    if (s.isNull())
        return;
    const int w = s.width(), h = s.height();
    if (w <= 0 || h <= 0)
        return;
    const double az = qDegreesToRadians(azimuth);
    const double el = qDegreesToRadians(qBound(-89.9, elevation, 89.9));
    const double lx = std::cos(el) * std::cos(az);
    const double ly = std::cos(el) * std::sin(az);
    const double strength = qBound(-5.0, depth, 5.0) / 100.0;
    // The neighbours are read from an unmodified copy: sampling the surface
    // that is being written would feed the result back into itself.
    const Surface src = s.copy();
    const SelReader sr(sel, w, h);

    for (int y = 0; y < h; ++y) {
        pixel_t* row = s.scanLine(y);
        for (int x = 0; x < w; ++x) {
            const quint8 cov = sr.at(x, y);
            if (cov == 0)
                continue;
            const pixel_t p = row[x];
            const quint8 a = getA(p);
            if (a == 0)
                continue;
            const double gx = (lumaAt(src, x + 1, y) - lumaAt(src, x - 1, y)) * 0.5;
            const double gy = (lumaAt(src, x, y + 1) - lumaAt(src, x, y - 1)) * 0.5;
            const double d = (gx * lx + gy * ly) * strength * 255.0;
            const int r = clamp255d(255.0 - (uR(p) + d));
            const int g = clamp255d(255.0 - (uG(p) + d));
            const int b = clamp255d(255.0 - (uB(p) + d));
            row[x] = mixCov(p, qPremult(a, quint8(r), quint8(g), quint8(b)), cov);
        }
    }
}

// ==============================================================
//  Edge detect (directional, like the emboss family)
// ==============================================================
void edgeDetect(Surface& s, const Selection& sel, double depth, double azimuth, double elevation)
{
    if (s.isNull())
        return;
    const int w = s.width(), h = s.height();
    if (w <= 0 || h <= 0)
        return;
    const double az = qDegreesToRadians(azimuth);
    const double el = qDegreesToRadians(qBound(-89.9, elevation, 89.9));
    const double lx = std::cos(el) * std::cos(az);
    const double ly = std::cos(el) * std::sin(az);
    const double strength = qBound(0.0, depth, 5.0) / 50.0;
    const Surface src = s.copy();
    const SelReader sr(sel, w, h);

    for (int y = 0; y < h; ++y) {
        pixel_t* row = s.scanLine(y);
        for (int x = 0; x < w; ++x) {
            const quint8 cov = sr.at(x, y);
            if (cov == 0)
                continue;
            const pixel_t p = row[x];
            const quint8 a = getA(p);
            if (a == 0)
                continue;
            const double gx = (lumaAt(src, x + 1, y) - lumaAt(src, x - 1, y));
            const double gy = (lumaAt(src, x, y + 1) - lumaAt(src, x, y - 1));
            const double d = std::fabs(gx * lx + gy * ly) * strength;
            const int v = clamp255d(d);
            row[x] = mixCov(p, qPremult(a, quint8(v), quint8(v), quint8(v)), cov);
        }
    }
}

// ==============================================================
//  Sobel edges
// ==============================================================
void sobelEdges(Surface& s, const Selection& sel)
{
    if (s.isNull())
        return;
    const int w = s.width(), h = s.height();
    if (w <= 0 || h <= 0)
        return;
    std::vector<quint8> mag;
    sobelMagnitude(s, mag);
    const SelReader sr(sel, w, h);
    for (int y = 0; y < h; ++y) {
        pixel_t* row = s.scanLine(y);
        for (int x = 0; x < w; ++x) {
            const quint8 cov = sr.at(x, y);
            if (cov == 0)
                continue;
            const pixel_t p = row[x];
            const quint8 a = getA(p);
            if (a == 0)
                continue;
            const int v = clamp255d(double(mag[size_t(y) * w + x]) * 2.0);
            row[x] = mixCov(p, qPremult(a, quint8(v), quint8(v), quint8(v)), cov);
        }
    }
}

namespace {

/// Local unsharp mask. ImageMath::unsharpMask() cannot be used here: it relies
/// on the broken pnq::getR() and it writes straight (non premultiplied) values
/// into a premultiplied buffer.
void unsharpMaskLocal(Surface& work, double radius, double amount, int threshold)
{
    if (work.isNull() || amount == 0.0)
        return;
    Surface blurred = work.copy();
    ImageMath::convolve(blurred, ImageMath::gaussianKernel(float(radius)));
    const int w = work.width(), h = work.height();
    if (w <= 0 || h <= 0)
        return;
    for (int y = 0; y < h; ++y) {
        pixel_t* s = work.scanLine(y);
        const pixel_t* b = blurred.scanLine(y);
        for (int x = 0; x < w; ++x) {
            const quint8 a = getA(s[x]);
            if (a == 0) {
                s[x] = 0;
                continue;
            }
            const int blur[3] = { uR(b[x]), uG(b[x]), uB(b[x]) };
            int ch[3] = { uR(s[x]), uG(s[x]), uB(s[x]) };
            for (int c = 0; c < 3; ++c) {
                const int diff = ch[c] - blur[c];
                if (std::abs(diff) < threshold)
                    continue;
                ch[c] = qBound(0, ch[c] + int(std::lround(double(diff) * amount / 100.0)), 255);
            }
            s[x] = qPremult(a, quint8(ch[0]), quint8(ch[1]), quint8(ch[2]));
        }
    }
}

} // namespace

// ==============================================================
//  Sharpen
// ==============================================================
void sharpen(Surface& s, const Selection& sel, double amount, double radius, bool monochrome)
{
    if (s.isNull() || amount <= 0.0)
        return;
    Surface work = s.copy();
    if (monochrome)
        makeMonochrome(work);
    unsharpMaskLocal(work, radius, amount, 0);

    const int w = s.width(), h = s.height();
    const SelReader sr(sel, w, h);
    for (int y = 0; y < h; ++y) {
        pixel_t* row = s.scanLine(y);
        const pixel_t* src = work.scanLine(y);
        for (int x = 0; x < w; ++x) {
            const quint8 cov = sr.at(x, y);
            if (cov == 0)
                continue;
            row[x] = mixCov(row[x], src[x], cov);
        }
    }
}

// ==============================================================
//  Old film
// ==============================================================
void oldFilm(Surface& s, const Selection& sel, int intensity, int monochrome, int matrix, int noise,
             double vignette, double tint)
{
    if (s.isNull())
        return;
    const int w = s.width(), h = s.height();
    if (w <= 0 || h <= 0)
        return;
    const double f = qBound(0.0, double(intensity) / 100.0, 1.0);
    const double nAmp = qBound(0.0, double(noise) / 100.0, 1.0);
    const double vig = qBound(0.0, vignette / 100.0, 1.0);
    const double tn = qBound(0.0, tint / 100.0, 1.0);

    // 3x3 colour matrices (normalised 0..1 input).
    static const double kMatrices[4][9] = {
        { 1.00, 0.00, 0.00, 0.00, 1.00, 0.00, 0.00, 0.00, 1.00 }, // 0: original
        { 1.12, -0.08, 0.02, -0.06, 1.05, -0.02, 0.00, -0.05, 1.02 }, // 1: western
        { 0.393, 0.769, 0.189, 0.349, 0.686, 0.168, 0.272, 0.534, 0.131 }, // 2: sepia
        { 0.34, 0.52, 0.14, 0.33, 0.50, 0.17, 0.30, 0.46, 0.24 }, // 3: faded / noir
    };
    const double* m = kMatrices[qBound(0, matrix, 3)];

    const double cx = w * 0.5, cy = h * 0.5;
    const double maxd = std::sqrt(cx * cx + cy * cy);
    const SelReader sr(sel, w, h);

    for (int y = 0; y < h; ++y) {
        pixel_t* row = s.scanLine(y);
        for (int x = 0; x < w; ++x) {
            const quint8 cov = sr.at(x, y);
            if (cov == 0)
                continue;
            const pixel_t p = row[x];
            const quint8 a = getA(p);
            if (a == 0)
                continue;
            double r = uR(p), g = uG(p), b = uB(p);
            if (monochrome) {
                const double l = luma255(int(r), int(g), int(b));
                r = g = b = l;
            }
            if (f > 0.0) {
                const double nr = m[0] * r + m[1] * g + m[2] * b;
                const double ng = m[3] * r + m[4] * g + m[5] * b;
                const double nb = m[6] * r + m[7] * g + m[8] * b;
                r += (nr - r) * f;
                g += (ng - g) * f;
                b += (nb - b) * f;
            }
            if (tn > 0.0) {
                // Warm sepia tone.
                const double tr = 0.45 * r + 0.40 * g + 0.20 * b;
                const double tg = 0.30 * r + 0.45 * g + 0.16 * b;
                const double tb = 0.20 * r + 0.24 * g + 0.10 * b;
                r += (tr - r) * tn;
                g += (tg - g) * tn;
                b += (tb - b) * tn;
            }
            if (vig > 0.0) {
                const double dx = x - cx, dy = y - cy;
                const double d = std::sqrt(dx * dx + dy * dy) / std::max(1.0, maxd);
                const double k = 1.0 - vig * std::pow(d, 2.2) * 0.85;
                r *= k;
                g *= k;
                b *= k;
            }
            if (nAmp > 0.0) {
                const double n = (hash01(x, y, 0xA5A5u) - 0.5) * 2.0 * nAmp * 42.0;
                r += n;
                g += n;
                b += n;
            }
            row[x] = mixCov(p, qPremult(a, quint8(clamp255d(r)), quint8(clamp255d(g)), quint8(clamp255d(b))), cov);
        }
    }
}

// ==============================================================
//  Vignette
// ==============================================================
void vignette(Surface& s, const Selection& sel, int cx, int cy, double start, double end, double feather, bool invert,
              bool centerColor, double brightness, double saturation)
{
    if (s.isNull())
        return;
    const int w = s.width(), h = s.height();
    if (w <= 0 || h <= 0)
        return;
    const double fcx = cx < 0 ? w * 0.5 : std::min(double(cx), double(w - 1));
    const double fcy = cy < 0 ? h * 0.5 : std::min(double(cy), double(h - 1));
    const double s0 = std::max(0.0, start);
    const double e0 = std::max(s0 + 1e-3, end);
    const double fe = std::max(0.0, feather);
    const double bri = qBound(-1.0, brightness / 100.0, 1.0);
    const double sat = qBound(0.0, saturation / 100.0, 1.0);
    const SelReader sr(sel, w, h);

    for (int y = 0; y < h; ++y) {
        pixel_t* row = s.scanLine(y);
        for (int x = 0; x < w; ++x) {
            const quint8 cov = sr.at(x, y);
            if (cov == 0)
                continue;
            const pixel_t p = row[x];
            const quint8 a = getA(p);
            if (a == 0)
                continue;
            const double dx = x - fcx, dy = y - fcy;
            const double d = std::sqrt(dx * dx + dy * dy);
            double t = (d - s0) / (e0 - s0);
            t = qBound(0.0, t, 1.0);
            if (fe > 1e-6) {
                // Soften the transition around the middle of the ramp.
                const double w2 = fe;
                t = qBound(0.0, (t - (0.5 - w2)) / std::max(1e-6, 2.0 * w2), 1.0);
                t = t * t * (3.0 - 2.0 * t);
            }
            if (invert)
                t = 1.0 - t;

            double r = uR(p), g = uG(p), b = uB(p);
            if (centerColor) {
                // Neutral grey core fading out towards the edge.
                const double k = std::pow(1.0 - t, 2.0);
                r += (128.0 - r) * k;
                g += (128.0 - g) * k;
                b += (128.0 - b) * k;
            }
            if (sat > 0.0) {
                const double l = luma255(int(r), int(g), int(b));
                const double k = t * sat;
                r += (l - r) * k;
                g += (l - g) * k;
                b += (l - b) * k;
            }
            if (std::fabs(bri) > 1e-6) {
                // Negative brightness darkens the outside, positive lightens.
                const double k = 1.0 + bri * t;
                r *= k;
                g *= k;
                b *= k;
            }
            row[x] = mixCov(p, qPremult(a, quint8(clamp255d(r)), quint8(clamp255d(g)), quint8(clamp255d(b))), cov);
        }
    }
}

// ==============================================================
//  Pixelate
// ==============================================================
void pixelate(Surface& s, const Selection& sel, int blockWidth, int blockHeight, bool normal)
{
    if (s.isNull())
        return;
    const int w = s.width(), h = s.height();
    if (w <= 0 || h <= 0)
        return;
    const int bw = qBound(1, blockWidth, w);
    const int bh = qBound(1, blockHeight, h);
    if (bw == 1 && bh == 1)
        return;
    const Surface src = s.copy();
    const SelReader sr(sel, w, h);

    for (int by = 0; by < h; by += bh) {
        const int y1 = std::min(by + bh, h);
        for (int bx = 0; bx < w; bx += bw) {
            const int x1 = std::min(bx + bw, w);
            long long a = 0, r = 0, g = 0, b = 0, n = 0;
            for (int y = by; y < y1; ++y) {
                const pixel_t* row = src.scanLine(y);
                for (int x = bx; x < x1; ++x) {
                    const pixel_t p = row[x];
                    a += chanA(p);
                    r += chanR(p);
                    g += chanG(p);
                    b += chanB(p);
                    ++n;
                }
            }
            if (n == 0)
                continue;
            const quint32 av = quint32(a / n);
            const pixel_t avg = (av << 24) | (std::min(av, quint32(r / n)) << 16)
                                | (std::min(av, quint32(g / n)) << 8) | std::min(av, quint32(b / n));
            // "Normal" = averaged blocks, otherwise the block keeps its first pixel.
            const pixel_t flat = normal ? avg : src.scanLine(by)[bx];
            for (int y = by; y < y1; ++y) {
                pixel_t* row = s.scanLine(y);
                for (int x = bx; x < x1; ++x) {
                    const quint8 cov = sr.at(x, y);
                    if (cov == 0)
                        continue;
                    row[x] = mixCov(row[x], flat, cov);
                }
            }
        }
    }
}

// ==============================================================
//  Mosaic
// ==============================================================
void mosaic(Surface& s, const Selection& sel, int cellSize, int sample)
{
    if (s.isNull())
        return;
    const int w = s.width(), h = s.height();
    if (w <= 0 || h <= 0)
        return;
    const int cs = qBound(1, cellSize, std::max(w, h));
    if (cs == 1)
        return;
    const int nSamples = qBound(1, sample > 0 ? sample : 12, 128);
    const Surface src = s.copy();
    const SelReader sr(sel, w, h);

    for (int cy = 0; cy < h; cy += cs) {
        for (int cx = 0; cx < w; cx += cs) {
            const int x1 = std::min(cx + cs, w);
            const int y1 = std::min(cy + cs, h);
            long long a = 0, r = 0, g = 0, b = 0;
            for (int i = 0; i < nSamples; ++i) {
                const quint32 hh = hash32(quint32(cx) * 73856093u ^ quint32(cy) * 19349663u
                                          ^ quint32(i) * 83492791u);
                const int xx = cx + int(hh % quint32(x1 - cx));
                const int yy = cy + int((hh >> 10) % quint32(y1 - cy));
                const pixel_t p = src.scanLine(yy)[xx];
                a += chanA(p);
                r += chanR(p);
                g += chanG(p);
                b += chanB(p);
            }
            const quint32 av = quint32(a / nSamples);
            const pixel_t flat = (av << 24) | (std::min(av, quint32(r / nSamples)) << 16)
                                 | (std::min(av, quint32(g / nSamples)) << 8)
                                 | std::min(av, quint32(b / nSamples));
            for (int y = cy; y < y1; ++y) {
                pixel_t* row = s.scanLine(y);
                for (int x = cx; x < x1; ++x) {
                    const quint8 cov = sr.at(x, y);
                    if (cov == 0)
                        continue;
                    row[x] = mixCov(row[x], flat, cov);
                }
            }
        }
    }
}

// ==============================================================
//  Cel shading
// ==============================================================
void celShading(Surface& s, const Selection& sel, int levels, double threshold, double smoothing)
{
    if (s.isNull())
        return;
    const int lv = qBound(2, levels, 64);
    const double th = qBound(0.0, threshold, 0.99);
    const int sm = std::max(1e-4, smoothing);

    forEachPixel(s, sel, [lv, th, sm](int, int, pixel_t& p) {
        const quint8 a = getA(p);
        if (a == 0)
            return;
        const double r = uR(p), g = uG(p), b = uB(p);
        const double l = 0.299 * r + 0.587 * g + 0.114 * b;
        const double scaled = l * lv / 255.0;
        const double fl = std::floor(scaled);
        const double frac = scaled - fl;
        // Soft threshold: keeps the band borders from being jagged.
        double t = (frac - th) / sm;
        t = t < 0 ? 0 : (t > 1 ? 1 : t);
        t = t * t * (3.0 - 2.0 * t);
        const double q = (fl + t) * 255.0 / lv;
        const double k = l > 0.5 ? q / l : 0.0;
        p = qPremult(a, quint8(clamp255d(r * k)), quint8(clamp255d(g * k)), quint8(clamp255d(b * k)));
    });
}

// ==============================================================
//  Oil paint
// ==============================================================
void oilPaint(Surface& s, const Selection& sel, int radius, int levels)
{
    if (s.isNull())
        return;
    const int w = s.width(), h = s.height();
    if (w <= 0 || h <= 0)
        return;
    const int r = qBound(1, radius, 12);
    const int lv = qBound(2, levels, 64);
    const double scale = lv / 255.0;
    const Surface src = s.copy();
    const SelReader sr(sel, w, h);

    for (int y = 0; y < h; ++y) {
        pixel_t* row = s.scanLine(y);
        for (int x = 0; x < w; ++x) {
            const quint8 cov = sr.at(x, y);
            if (cov == 0)
                continue;
            long long cnt[64] = { 0 };
            long long sr_[64] = { 0 }, sg_[64] = { 0 }, sb_[64] = { 0 }, sa_[64] = { 0 };
            for (int dy = -r; dy <= r; ++dy) {
                const int yy = y + dy;
                if (yy < 0 || yy >= h)
                    continue;
                const pixel_t* srow = src.scanLine(yy);
                for (int dx = -r; dx <= r; ++dx) {
                    const int xx = x + dx;
                    if (xx < 0 || xx >= w)
                        continue;
                    const pixel_t q = srow[xx];
                    const int l = luma255(uR(q), uG(q), uB(q));
                    int bin = int(l * scale);
                    bin = qBound(0, bin, lv - 1);
                    if (bin < 0)
                        bin = 0;
                    if (bin > 63)
                        bin = 63;
                    ++cnt[bin];
                    sr_[bin] += chanR(q);
                    sg_[bin] += chanG(q);
                    sb_[bin] += chanB(q);
                    sa_[bin] += chanA(q);
                }
            }
            int best = -1;
            long long bestc = 0;
            for (int i = 0; i < lv; ++i) {
                if (cnt[i] > bestc) {
                    bestc = cnt[i];
                    best = i;
                }
            }
            if (best < 0 || bestc == 0)
                continue;
            const pixel_t p = row[x];
            const quint32 av = quint32(sa_[best] / bestc);
            const quint32 rv = std::min(av, quint32(sr_[best] / bestc));
            const quint32 gv = std::min(av, quint32(sg_[best] / bestc));
            const quint32 bv = std::min(av, quint32(sb_[best] / bestc));
            row[x] = mixCov(p, (av << 24) | (rv << 16) | (gv << 8) | bv, cov);
        }
    }
}

// ==============================================================
//  Posterize edges
// ==============================================================
void posterizeEdges(Surface& s, const Selection& sel, int posterizeLevels, int edgePosterizeLevels,
                    int edgeThreshold, int edgeThickness)
{
    if (s.isNull())
        return;
    const int w = s.width(), h = s.height();
    if (w <= 0 || h <= 0)
        return;
    const int pl = qBound(2, posterizeLevels, 255);
    const int el = qBound(2, edgePosterizeLevels, 255);
    const double thr = std::pow(qBound(0.0, double(edgeThreshold) / 100.0, 1.0), 2.0) * 255.0;
    const int th = qMax(1, edgeThickness);

    std::vector<quint8> mag;
    sobelMagnitude(s, mag);

    const double ps = 255.0 / (pl - 1);
    const double es = 255.0 / (el - 1);
    const SelReader sr(sel, w, h);
    const auto quant = [](int v, double step) { return int(clamp255d(std::floor(v / step + 0.5) * step)); };

    for (int y = 0; y < h; ++y) {
        pixel_t* row = s.scanLine(y);
        for (int x = 0; x < w; ++x) {
            const quint8 cov = sr.at(x, y);
            if (cov == 0)
                continue;
            const pixel_t p = row[x];
            const quint8 a = getA(p);
            if (a == 0)
                continue;
            bool edge = false;
            for (int dy = -th; dy <= th && !edge; dy += th) {
                for (int dx = -th; dx <= th; dx += th) {
                    const int xx = qBound(0, x + dx, w - 1);
                    const int yy = qBound(0, y + dy, h - 1);
                    if (mag[size_t(yy) * w + xx] > thr) {
                        edge = true;
                        break;
                    }
                }
            }
            int r, g, b;
            if (edge) {
                r = quant(uR(p), es);
                g = quant(uG(p), es);
                b = quant(uB(p), es);
            } else {
                r = quant(uR(p), ps);
                g = quant(uG(p), ps);
                b = quant(uB(p), ps);
            }
            row[x] = mixCov(p, qPremult(a, quint8(r), quint8(g), quint8(b)), cov);
        }
    }
}

// ==============================================================
//  Diffuse glow
// ==============================================================
void diffuseGlow(Surface& s, const Selection& sel, double amount, double chroma, int iterations)
{
    if (s.isNull())
        return;
    const double f = qBound(0.0, amount / 100.0, 2.0);
    if (f <= 0.0)
        return;
    const int iters = qBound(1, iterations, 40);
    const double chrom = qBound(0.0, chroma, 2.0);

    Surface acc = s.copy();
    acc.fill(0);
    Surface glow = s.copy();
    for (int i = 0; i < iters; ++i) {
        for (int y = 0; y < acc.height(); ++y) {
            pixel_t* ar = acc.scanLine(y);
            const pixel_t* gr = glow.scanLine(y);
            for (int x = 0; x < acc.width(); ++x)
                ar[x] = addPixels(scalePix(ar[x], 0.82), gr[x]);
        }
        boxBlurPremult(glow, 2);
    }

    const int w = s.width(), h = s.height();
    const SelReader sr(sel, w, h);
    for (int y = 0; y < h; ++y) {
        pixel_t* row = s.scanLine(y);
        const pixel_t* ar = acc.scanLine(y);
        for (int x = 0; x < w; ++x) {
            const quint8 cov = sr.at(x, y);
            if (cov == 0)
                continue;
            pixel_t g = ar[x];
            if (chrom > 0.0) {
                // Boost the chroma of the glow around its own luma.
                const quint32 av = chanA(g);
                const int l = luma255(chanR(g), chanG(g), chanB(g));
                const double k = 1.0 + chrom;
                const quint32 r = std::min(av, quint32(qBound(0.0, (l + (chanR(g) - l) * k), 255.0)));
                const quint32 gg = std::min(av, quint32(qBound(0.0, (l + (chanG(g) - l) * k), 255.0)));
                const quint32 b = std::min(av, quint32(qBound(0.0, (l + (chanB(g) - l) * k), 255.0)));
                g = (av << 24) | (r << 16) | (gg << 8) | b;
            }
            g = scalePix(g, f * 0.5);
            if (chanA(g) == 0)
                continue;
            if (cov < 255)
                g = scalePix(g, double(cov) / 255.0);
            row[x] = addPixels(row[x], g);
        }
    }
}

// ==============================================================
//  Glow (warped)
// ==============================================================
void glowWarped(Surface& s, const Selection& sel, int radius, int intensity, int warp, int radial, pixel_t color,
                bool centerAura)
{
    if (s.isNull())
        return;
    const int w = s.width(), h = s.height();
    if (w <= 0 || h <= 0)
        return;
    const int rad = qBound(0, radius, 500);
    const double inten = qBound(0.0, double(intensity) / 100.0, 2.0);
    const double warpAmp = double(warp) * 0.25;
    const double radialK = 1.0 - double(radial) / 100.0;
    if (rad == 0 && warp == 0 && radial == 0 && !centerAura)
        return;
    const quint8 cr = uR(color), cg = uG(color), cb = uB(color);

    QImage a = extractAlpha(s);
    if (rad > 0)
        ImageMath::gaussianBlurAlpha(a, float(rad));
    const double maxd = std::sqrt(double(w) * w + double(h) * h) * 0.5;
    const SelReader sr(sel, w, h);

    for (int y = 0; y < h; ++y) {
        pixel_t* row = s.scanLine(y);
        const uchar* ar = a.constScanLine(y);
        for (int x = 0; x < w; ++x) {
            const quint8 cov = sr.at(x, y);
            if (cov == 0)
                continue;
            double sx = x, sy = y;
            if (warpAmp > 0.0) {
                const double n = fbm(x * 0.02, y * 0.02, 3, 0x1234567u) - 0.5;
                const double m = fbm(x * 0.02 + 31.7, y * 0.02 + 11.3, 3, 0x89abcdu) - 0.5;
                sx += n * warpAmp * 2.0;
                sy += m * warpAmp * 2.0;
            }
            if (radial != 0) {
                const double dx = sx - w * 0.5, dy = sy - h * 0.5;
                const double d = std::sqrt(dx * dx + dy * dy);
                const double k = d * 2.0 * radialK / std::max(1.0, maxd);
                sx = w * 0.5 + dx * k;
                sy = h * 0.5 + dy * k;
            }
            const int ax = qBound(0, int(sx), w - 1);
            const int ay = qBound(0, int(sy), h - 1);
            // The glow is sampled from the (warped) alpha field.
            double ga = double(ar[ay * a.width() + ax]) * inten / 255.0;
            if (centerAura) {
                const double dx = x - w * 0.5, dy = y - h * 0.5;
                const double dd = std::sqrt(dx * dx + dy * dy) / std::max(1.0, maxd);
                ga = std::max(ga, std::pow(std::max(0.0, 1.0 - dd), 2.0) * inten * 0.75);
            }
            if (ga <= 0.5)
                continue;
            pixel_t gpx = qPremult(quint8(qBound(0.0, ga, 255.0)), cr, cg, cb);
            if (cov < 255)
                gpx = scalePix(gpx, double(cov) / 255.0);
            row[x] = addPixels(row[x], gpx);
        }
    }
}

// ==============================================================
//  Crystalize
// ==============================================================
void crystalize(Surface& s, const Selection& sel, int cellSize)
{
    if (s.isNull())
        return;
    const int w = s.width(), h = s.height();
    if (w <= 0 || h <= 0)
        return;
    const int cs = qBound(1, cellSize, 256);
    if (cs == 1)
        return;
    const Surface src = s.copy();
    const SelReader sr(sel, w, h);

    for (int y = 0; y < h; ++y) {
        pixel_t* row = s.scanLine(y);
        for (int x = 0; x < w; ++x) {
            const quint8 cov = sr.at(x, y);
            if (cov == 0)
                continue;
            const int ccx = x / cs, ccy = y / cs;
            double bestD = 1e30;
            pixel_t best = src.scanLine(y)[x];
            // Search the 3x3 neighbourhood of cells (Voronoi cells).
            for (int oy = -1; oy <= 1; ++oy) {
                for (int ox = -1; ox <= 1; ++ox) {
                    const int cx = ccx + ox, cy = ccy + oy;
                    const quint32 hh = hash32(quint32(cx) * 0x9E3779B9u ^ quint32(cy) * 0x85EBCA6Bu ^ 0x51ED270Bu);
                    const int sx = cx * cs + int(hh % quint32(cs));
                    const int sy = cy * cs + int((hh >> 11) % quint32(cs));
                    const int px = qBound(0, sx, w - 1);
                    const int py = qBound(0, sy, h - 1);
                    const double dx = x - px, dy = y - py;
                    const double d = dx * dx + dy * dy;
                    if (d < bestD) {
                        bestD = d;
                        best = src.scanLine(py)[px];
                    }
                }
            }
            row[x] = mixCov(row[x], best, cov);
        }
    }
}

// ==============================================================
//  Ripple
// ==============================================================
void ripple(Surface& s, const Selection& sel, double amplitude, double frequency, double phase, int direction)
{
    if (s.isNull())
        return;
    const int w = s.width(), h = s.height();
    if (w <= 0 || h <= 0)
        return;
    const double amp = std::fabs(amplitude);
    if (amp < 0.01)
        return;
    const double freq = std::max(0.0001, std::fabs(frequency));
    const bool horizontal = (direction % 2) == 0;
    const double period = horizontal ? std::max(4.0, double(h)) : std::max(4.0, double(w));
    const Surface src = s.copy();
    const SelReader sr(sel, w, h);

    for (int y = 0; y < h; ++y) {
        pixel_t* row = s.scanLine(y);
        for (int x = 0; x < w; ++x) {
            const quint8 cov = sr.at(x, y);
            if (cov == 0)
                continue;
            double sx = x, sy = y;
            if (horizontal) {
                sx = x + amp * std::sin(2.0 * M_PI * freq * y / period + phase);
            } else {
                sy = y + amp * std::sin(2.0 * M_PI * freq * x / period + phase);
            }
            row[x] = mixCov(row[x], sampleBilinear(src, sx, sy), cov);
        }
    }
}

// ==============================================================
//  Water colour
// ==============================================================
void waterColor(Surface& s, const Selection& sel, int distortion)
{
    if (s.isNull())
        return;
    const int w = s.width(), h = s.height();
    if (w <= 0 || h <= 0)
        return;
    const int r = qBound(1, distortion, 40);
    const Surface src = s.copy();
    const SelReader sr(sel, w, h);
    // Four coherent, smooth displacement fields: neighbouring pixels sample
    // almost the same spot, so flat areas stay flat and only the contours bleed.
    const double k = 1.0 / std::max(1.0, double(r) * 0.75);

    for (int y = 0; y < h; ++y) {
        pixel_t* row = s.scanLine(y);
        for (int x = 0; x < w; ++x) {
            const quint8 cov = sr.at(x, y);
            if (cov == 0)
                continue;
            const pixel_t centre = src.scanLine(y)[x];
            const quint8 a = getA(centre);
            if (a == 0)
                continue;
            long long accR = getR(centre) * 2, accG = getG(centre) * 2, accB = getB(centre) * 2;
            int cnt = 2;
            for (int i = 0; i < 6; ++i) {
                const double ang = (double(i) + 0.5) * (2.0 * M_PI / 6.0);
                const double ox = valueNoise(x * k, y * k, 0x1234567u + quint32(i) * 131u) * 2.0 - 1.0;
                const double oy = valueNoise(x * k, y * k, 0x89abcdu + quint32(i) * 977u) * 2.0 - 1.0;
                const double dx = (std::cos(ang) + ox * 0.9) * r;
                const double dy = (std::sin(ang) + oy * 0.9) * r;
                const pixel_t p = sampleBilinear(src, x + dx, y + dy);
                accR += uR(p);
                accG += uG(p);
                accB += uB(p);
                ++cnt;
            }
            const int nr = clamp255d(double(accR) / cnt);
            const int ng = clamp255d(double(accG) / cnt);
            const int nb = clamp255d(double(accB) / cnt);
            // Slight local contrast boost: watercolour pigment look.
            const auto boost = [&](int c, int o) { return clamp255d(double(o) + (double(c) - double(o)) * 1.12); };
            row[x] = mixCov(centre,
                            qPremult(a, quint8(boost(nr, uR(centre))), quint8(boost(ng, uG(centre))),
                                     quint8(boost(nb, uB(centre)))),
                            cov);
        }
    }
}

// ==============================================================
//  Sunburst
// ==============================================================
void sunburst(Surface& s, const Selection& sel, int cx, int cy, int rays, double brightness, bool invert)
{
    if (s.isNull())
        return;
    const int w = s.width(), h = s.height();
    if (w <= 0 || h <= 0)
        return;
    const int nRays = qBound(2, rays, 512);
    const double fcx = cx < 0 ? w * 0.5 : std::min(double(cx), double(w - 1));
    const double fcy = cy < 0 ? h * 0.5 : std::min(double(cy), double(h - 1));
    const double maxd = std::max(1.0, std::sqrt(double(w) * w + double(h) * h) * 0.5);
    const double bri = qBound(-1.0, brightness, 2.0);
    const Surface src = s.copy();
    const SelReader sr(sel, w, h);
    const int samples = 10;

    for (int y = 0; y < h; ++y) {
        pixel_t* row = s.scanLine(y);
        for (int x = 0; x < w; ++x) {
            const quint8 cov = sr.at(x, y);
            if (cov == 0)
                continue;
            const double ox = x - fcx, oy = y - fcy;
            const double r = std::sqrt(ox * ox + oy * oy);
            const double theta = std::atan2(oy, ox);
            // The angular step between two rays narrows with the radius.
            const double dTheta = 2.0 * M_PI / nRays;
            const double twist = std::floor(theta / dTheta + 0.5) * dTheta;
            double a = 0.0, rr = 0.0, gg = 0.0, bb = 0.0;
            for (int i = 0; i < samples; ++i) {
                const double t = double(i) / double(samples - 1);
                const double sr2 = r * (1.0 - t * 0.92);
                const double ang = twist + (theta - twist) * (1.0 - t * 0.25);
                const pixel_t p = sampleBilinear(src, fcx + std::cos(ang) * sr2, fcy + std::sin(ang) * sr2);
                a += chanA(p);
                rr += chanR(p);
                gg += chanG(p);
                bb += chanB(p);
            }
            const auto c8 = [](double v) { return quint32(v < 0.0 ? 0.0 : (v > 255.0 ? 255.0 : v + 0.5)); };
            const quint32 av = c8(a / samples);
            quint32 r8 = c8(rr / samples), g8 = c8(gg / samples), b8 = c8(bb / samples);
            // Brightness / darkening falloff towards the rim.
            double k = 1.0 + (invert ? -1.0 : 1.0) * bri * (1.0 - r / (maxd * 2.0));
            k = qBound(0.0, k, 2.0);
            r8 = std::min(av, quint32(qBound(0.0, r8 * k, 255.0)));
            g8 = std::min(av, quint32(qBound(0.0, g8 * k, 255.0)));
            b8 = std::min(av, quint32(qBound(0.0, b8 * k, 255.0)));
            row[x] = mixCov(row[x], (av << 24) | (r8 << 16) | (g8 << 8) | b8, cov);
        }
    }
}

// ==============================================================
//  Recursive descent
// ==============================================================
void recursiveDescent(Surface& s, const Selection& sel, double strength, double monotone)
{
    if (s.isNull())
        return;
    const int w = s.width(), h = s.height();
    if (w <= 0 || h <= 0)
        return;
    const double f = qBound(0.0, strength / 100.0, 1.0);
    if (f <= 0.0)
        return;
    const double mono = qBound(0.0, monotone, 1.0);

    // Mean colour of the selected area.
    double mr = 0, mg = 0, mb = 0;
    long long n = 0;
    const SelReader sr(sel, w, h);
    for (int y = 0; y < h; ++y) {
        const pixel_t* row = s.scanLine(y);
        for (int x = 0; x < w; ++x) {
            if (sr.at(x, y) == 0)
                continue;
            const pixel_t p = row[x];
            const quint8 a = getA(p);
            if (a == 0)
                continue;
            mr += uR(p);
            mg += uG(p);
            mb += uB(p);
            ++n;
        }
    }
    if (n == 0)
        return;
    mr /= double(n);
    mg /= double(n);
    mb /= double(n);
    const double ml = 0.299 * mr + 0.587 * mg + 0.114 * mb;

    const int steps = 12;
    for (int it = 0; it < steps; ++it) {
        // Converging: each pass moves a bit closer to the mean.
        const double k = f * (1.0 - double(it) / double(steps)) * 0.5;
        if (k <= 0.0)
            break;
        for (int y = 0; y < h; ++y) {
            pixel_t* row = s.scanLine(y);
            for (int x = 0; x < w; ++x) {
                const quint8 cov = sr.at(x, y);
                if (cov == 0)
                    continue;
                const pixel_t p = row[x];
                const quint8 a = getA(p);
                if (a == 0)
                    continue;
                double r = uR(p), g = uG(p), b = uB(p);
                if (mono > 0.0) {
                    const double l = 0.299 * r + 0.587 * g + 0.114 * b;
                    r += ((l + (ml - l) * mono) - r) * k;
                    g += ((l + (ml - l) * mono) - g) * k;
                    b += ((l + (ml - l) * mono) - b) * k;
                } else {
                    r += (mr - r) * k;
                    g += (mg - g) * k;
                    b += (mb - b) * k;
                }
                row[x] = mixCov(p, qPremult(a, quint8(clamp255d(r)), quint8(clamp255d(g)), quint8(clamp255d(b))), cov);
            }
        }
    }
}

// ==============================================================
//  Unsharp mask
// ==============================================================
void unsharpMask(Surface& s, const Selection& sel, double amount, double radius, int threshold)
{
    if (s.isNull() || amount <= 0.0)
        return;
    Surface work = s.copy();
    unsharpMaskLocal(work, radius, amount, threshold);
    const int w = s.width(), h = s.height();
    const SelReader sr(sel, w, h);
    for (int y = 0; y < h; ++y) {
        pixel_t* row = s.scanLine(y);
        const pixel_t* src = work.scanLine(y);
        for (int x = 0; x < w; ++x) {
            const quint8 cov = sr.at(x, y);
            if (cov == 0)
                continue;
            row[x] = mixCov(row[x], src[x], cov);
        }
    }
}

} // namespace Effects
} // namespace pnq
