// ==============================================================
//  NoiseEffects.cpp - noise generation and filters.
// ==============================================================
#include "effects/Effects.h"

#include "core/ColorUtils.h"
#include "core/ImageMath.h"

#include <QImage>
#include <QPoint>
#include <QtMath>
#include <QVector>

#include <algorithm>
#include <array>
#include <cmath>
#include <random>

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

/// Deterministic pseudo random value in 0..1 for an integer lattice point.
inline double hash01(int x, int y, quint32 seed)
{
    const quint32 h = hash32(quint32(x) * 0x9E3779B9u ^ (quint32(y) + 0x85EBCA6Bu) * 0xC2B2AE35u ^ seed);
    return double(h >> 8) / 16777215.0;
}

/// Signed deterministic pseudo random value in -1..1.
inline double hash11(int x, int y, quint32 seed)
{
    return hash01(x, y, seed) * 2.0 - 1.0;
}

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
    return norm > 1e-9 ? sum / norm : 0.0;
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

inline pixel_t lumaPixel(pixel_t p)
{
    const quint8 l = quint8(luma255(uR(p), uG(p), uB(p)));
    return qPremult(getA(p), l, l, l);
}

} // namespace

// ==============================================================
//  Add noise
// ==============================================================
void addNoise(Surface& s, const Selection& sel, int amount, bool uniform, bool monochrome, bool correlated,
              quint32 seed)
{
    if (s.isNull())
        return;
    const double amp = qBound(0.0, double(amount) / 100.0, 1.0);
    if (amp <= 0.0)
        return;
    // Deterministic generator: the same seed always gives the same grain.
    std::mt19937 rng(seed ? seed : 0x9E3779B9u);
    std::uniform_int_distribution<int> udist(-255, 255);
    std::normal_distribution<double> gdist(0.0, 1.0);
    const double uniformAmp = amp * 127.0;
    const double gaussAmp = amp * 95.0;
    const quint32 cseed = seed * 2654435761u + 0x51ED2701u;

    forEachPixel(s, sel, [&](int x, int y, pixel_t& p) {
        const quint8 a = getA(p);
        if (a == 0)
            return;
        double n1, n2, n3;
        if (correlated) {
            // Spatially correlated grain: a smooth field in [-amp, amp].
            n1 = fbm(x * 0.35, y * 0.35, 2, cseed, 0.6) * 2.0 - 1.0;
            n2 = fbm(x * 0.35 + 53.7, y * 0.35 + 17.3, 2, cseed + 977u, 0.6) * 2.0 - 1.0;
            n3 = fbm(x * 0.35 + 91.1, y * 0.35 + 63.9, 2, cseed + 6131u, 0.6) * 2.0 - 1.0;
            n1 *= gaussAmp;
            n2 *= gaussAmp;
            n3 *= gaussAmp;
        } else if (uniform) {
            n1 = udist(rng) * (uniformAmp / 255.0);
            n2 = udist(rng) * (uniformAmp / 255.0);
            n3 = udist(rng) * (uniformAmp / 255.0);
        } else {
            n1 = gdist(rng) * gaussAmp;
            n2 = gdist(rng) * gaussAmp;
            n3 = gdist(rng) * gaussAmp;
        }
        if (monochrome) {
            const double n = (n1 + n2 + n3) / 3.0;
            n1 = n2 = n3 = n;
        }
        p = qPremult(a, quint8(clamp255d(uR(p) + n1)), quint8(clamp255d(uG(p) + n2)),
                     quint8(clamp255d(uB(p) + n3)));
    });
}

// ==============================================================
//  Clouds
// ==============================================================
void clouds(Surface& s, const Selection& sel, double size, double seed, pixel_t baseColor)
{
    if (s.isNull())
        return;
    const double scale = 1.0 / std::max(0.25, size);
    const quint32 sd = quint32(std::llround(std::fabs(seed) * 1000.0)) * 2654435761u + 0x1B873593u;
    const double br = uR(baseColor), bg = uG(baseColor), bb = uB(baseColor);

    forEachPixel(s, sel, [scale, sd, br, bg, bb](int x, int y, pixel_t& p) {
        const quint8 a = getA(p);
        if (a == 0)
            return;
        const double n = fbm(x * scale, y * scale, 5, sd, 0.55, false);
        // Base colour modulated by the cloud density, with white highlights.
        double k = 0.55 + 0.85 * n;
        double r = br * k, g = bg * k, b = bb * k;
        const double hi = std::max(0.0, n - 0.62) * 1.6;
        r += (255.0 - r) * hi;
        g += (255.0 - g) * hi;
        b += (255.0 - b) * hi;
        p = qPremult(a, quint8(clamp255d(r)), quint8(clamp255d(g)), quint8(clamp255d(b)));
    });
}

// ==============================================================
//  Fractal noise
// ==============================================================
void fractalNoise(Surface& s, const Selection& sel, double octaveCount, double persistence, int seed,
                  bool turbulence, int channel)
{
    if (s.isNull())
        return;
    const int oct = qBound(1, int(std::lround(octaveCount)), 10);
    const double per = qBound(0.0, persistence, 1.0);
    const quint32 sd = quint32(seed) * 2654435761u + 0x27D4EB2Fu;
    const double scale = 1.0 / 64.0; // one noise cell every 64 px
    const int ch = channel;

    forEachPixel(s, sel, [&](int x, int y, pixel_t& p) {
        const quint8 a = getA(p);
        if (a == 0)
            return;
        const double n = fbm(x * scale, y * scale, oct, sd, per, turbulence);
        const int g = clamp255d(n * 255.0);
        if (ch == 1) {
            // Alpha channel only.
            const int na = clamp255d(a * (0.35 + 0.9 * n));
            const double k = a > 0 ? double(na) / double(a) : 1.0;
            p = qPremult(quint8(na), quint8(clamp255d(uR(p) * k)), quint8(clamp255d(uG(p) * k)),
                         quint8(clamp255d(uB(p) * k)));
            return;
        }
        const int r0 = uR(p), g0 = uG(p), b0 = uB(p);
        if (ch == 2) {
            // Additive / "smoke" mode.
            const int k = int(std::lround(n * 190.0));
            p = qPremult(a, quint8(clamp255d(r0 + k)), quint8(clamp255d(g0 + k)), quint8(clamp255d(b0 + k)));
            return;
        }
        const quint8 r = quint8(blendChannel(BlendMode::Overlay, r0, g));
        const quint8 gg = quint8(blendChannel(BlendMode::Overlay, g0, g));
        const quint8 b = quint8(blendChannel(BlendMode::Overlay, b0, g));
        p = qPremult(a, quint8((r + r0) / 2), quint8((gg + g0) / 2), quint8((b + b0) / 2));
    });
}

// ==============================================================
//  Turbulence
// ==============================================================
void turbulenceNoise(Surface& s, const Selection& sel, int seed)
{
    if (s.isNull())
        return;
    const quint32 sd = quint32(seed) * 2654435761u + 0x165667B1u;
    const double scale = 1.0 / 48.0;
    forEachPixel(s, sel, [sd, scale](int x, int y, pixel_t& p) {
        const quint8 a = getA(p);
        if (a == 0)
            return;
        const double n = fbm(x * scale, y * scale, 6, sd, 0.55, true);
        const int g = clamp255d(n * 255.0);
        const int r0 = uR(p), g0 = uG(p), b0 = uB(p);
        const int r = (int(blendChannel(BlendMode::Overlay, r0, g)) + r0) / 2;
        const int gg = (int(blendChannel(BlendMode::Overlay, g0, g)) + g0) / 2;
        const int b = (int(blendChannel(BlendMode::Overlay, b0, g)) + b0) / 2;
        p = qPremult(a, quint8(clamp255d(r)), quint8(clamp255d(gg)), quint8(clamp255d(b)));
    });
}

// ==============================================================
//  Median filter
// ==============================================================
void medianFilter(Surface& s, const Selection& sel, int radius)
{
    if (s.isNull())
        return;
    const int w = s.width(), h = s.height();
    if (w <= 0 || h <= 0)
        return;
    const int r = qBound(1, radius, 16);
    const Surface src = s.copy();
    const SelReader sr(sel, w, h);

    for (int y = 0; y < h; ++y) {
        pixel_t* row = s.scanLine(y);
        for (int x = 0; x < w; ++x) {
            const quint8 cov = sr.at(x, y);
            if (cov == 0)
                continue;
            const pixel_t centre = src.scanLine(y)[x];
            if (getA(centre) == 0)
                continue;
            int histR[256] = { 0 }, histG[256] = { 0 }, histB[256] = { 0 };
            int count = 0;
            for (int dy = -r; dy <= r; ++dy) {
                const int yy = y + dy;
                if (yy < 0 || yy >= h)
                    continue;
                const pixel_t* srow = src.scanLine(yy);
                for (int dx = -r; dx <= r; ++dx) {
                    const int xx = x + dx;
                    if (xx < 0 || xx >= w)
                        continue;
                    const pixel_t p = srow[xx];
                    ++histR[uR(p)];
                    ++histG[uG(p)];
                    ++histB[uB(p)];
                    ++count;
                }
            }
            if (count == 0)
                continue;
            const int target = count / 2;
            const auto pick = [&](int* hist) {
                int acc = 0;
                for (int i = 0; i < 256; ++i) {
                    acc += hist[i];
                    if (acc > target)
                        return i;
                }
                return 255;
            };
            const int mr = pick(histR);
            const int mg = pick(histG);
            const int mb = pick(histB);
            row[x] = mixCov(centre, qPremult(getA(centre), quint8(mr), quint8(mg), quint8(mb)), cov);
        }
    }
}

// ==============================================================
//  Surface noise (block noise)
// ==============================================================
void surfaceNoise(Surface& s, const Selection& sel, int size, double strength, bool monochrome, quint32 seed)
{
    if (s.isNull())
        return;
    const int w = s.width(), h = s.height();
    if (w <= 0 || h <= 0)
        return;
    const int cs = qBound(1, size, 512);
    if (cs == 1 && size == 1)
        return;
    const double f = qBound(0.0, strength / 100.0, 1.0);
    if (f <= 0.0)
        return;
    const double amp = f * 200.0;
    std::mt19937 rng(seed ? seed : 0x2545F491u);

    // One value per block, so neighbouring pixels stay identical.
    const int bw = (w + cs - 1) / cs;
    const int bh = (h + cs - 1) / cs;
    std::vector<double> r(bw * bh), g(bw * bh), b(bw * bh);
    std::uniform_real_distribution<double> dist(-1.0, 1.0);
    for (int i = 0; i < bw * bh; ++i) {
        if (monochrome) {
            const double v = dist(rng) * amp;
            r[size_t(i)] = g[size_t(i)] = b[size_t(i)] = v;
        } else {
            r[size_t(i)] = dist(rng) * amp;
            g[size_t(i)] = dist(rng) * amp;
            b[size_t(i)] = dist(rng) * amp;
        }
    }

    forEachPixel(s, sel, [&](int x, int y, pixel_t& p) {
        const quint8 a = getA(p);
        if (a == 0)
            return;
        const size_t i = size_t(y / cs) * bw + size_t(x / cs);
        p = qPremult(a, quint8(clamp255d(uR(p) + r[i])), quint8(clamp255d(uG(p) + g[i])),
                     quint8(clamp255d(uB(p) + b[i])));
    });
}

// ==============================================================
//  Stretch dents
// ==============================================================
void stretchDents(Surface& s, const Selection& sel, int size, int length, double amplitude, const QPoint& center)
{
    if (s.isNull())
        return;
    const int w = s.width(), h = s.height();
    if (w <= 0 || h <= 0)
        return;
    const double fcx = center.x() < 0 ? w * 0.5 : std::min(double(center.x()), double(w - 1));
    const double fcy = center.y() < 0 ? h * 0.5 : std::min(double(center.y()), double(h - 1));
    const int rad = qBound(1, size, std::max(w, h));
    const double len = std::max(0.0, double(length));
    const double amp = amplitude;
    if (len <= 0.0 || std::fabs(amp) < 1e-6)
        return;
    const Surface src = s.copy();
    const SelReader sr(sel, w, h);

    const int x0 = qMax(0, int(fcx) - rad);
    const int x1 = qMin(w - 1, int(fcx) + rad);
    const int y0 = qMax(0, int(fcy) - rad);
    const int y1 = qMin(h - 1, int(fcy) + rad);

    for (int y = y0; y <= y1; ++y) {
        pixel_t* row = s.scanLine(y);
        for (int x = x0; x <= x1; ++x) {
            const quint8 cov = sr.at(x, y);
            if (cov == 0)
                continue;
            const double dx = x - fcx, dy = y - fcy;
            const double r = std::sqrt(dx * dx + dy * dy);
            if (r > rad)
                continue;
            if (r < 1e-6)
                continue;
            // Coherent noise field around the centre (angular + radial).
            const double ux = dx / r, uy = dy / r;
            const double nx = valueNoise(ux * 3.0 + 40.0, r * 0.06, 0x5BD1E995u) * 2.0 - 1.0;
            const double ny = valueNoise(ux * 3.0 + 40.0, r * 0.06, 0x27D4EB2Fu) * 2.0 - 1.0;
            // Fade out towards the border of the influence area.
            const double fall = 1.0 - std::pow(r / rad, 2.0);
            const double dr = nx * len * amp * fall;
            const double dt = ny * len * amp * 0.5 * fall;
            // Radial stretch plus a tangential swirl.
            const double nr = r + dr;
            const double tx = -uy, ty = ux;
            const double sx = fcx + ux * nr + tx * dt;
            const double sy = fcy + uy * nr + ty * dt;
            row[x] = mixCov(row[x], sampleBilinear(src, sx, sy), cov);
        }
    }
}

} // namespace Effects
} // namespace pnq
