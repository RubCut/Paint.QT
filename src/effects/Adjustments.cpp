// ==============================================================
//  Adjustments.cpp - colour adjustment effects.
// ==============================================================
#include "effects/Effects.h"

#include "core/ColorUtils.h"
#include "core/ImageMath.h"

#include <QImage>
#include <QtMath>
#include <QVector>

#include <algorithm>
#include <cmath>
#include <vector>

namespace pnq {
namespace Effects {
namespace {

// -------------------------------------------------------------- small utils
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

inline int luma255(int r, int g, int b)
{
    return clamp255d(0.299 * r + 0.587 * g + 0.114 * b);
}

/// Interprets a UI value that may be given either in percent (0..100) or in
/// absolute 0..255 units.
inline double pctOrAbs(int v)
{
    if (v > 100)
        return qBound(0.0, double(v) / 255.0, 1.0);
    return qBound(0.0, double(v) / 100.0, 1.0);
}

struct Hsl
{
    float h = 0.f; ///< 0..360
    float s = 0.f; ///< 0..1
    float l = 0.f; ///< 0..1
};

Hsl rgbToHslF(int r, int g, int b)
{
    Hsl out;
    const double rf = r / 255.0, gf = g / 255.0, bf = b / 255.0;
    const double mx = std::max(rf, std::max(gf, bf));
    const double mn = std::min(rf, std::min(gf, bf));
    out.l = float((mx + mn) * 0.5);
    const double d = mx - mn;
    if (d < 1e-9) {
        out.h = 0.f;
        out.s = 0.f;
        return out;
    }
    out.s = float(out.l > 0.9999 ? 1.0 : (out.l < 1e-6 ? 0.0 : d / (1.0 - std::fabs(2.0 * out.l - 1.0))));
    double h;
    if (mx == rf)
        h = 60.0 * std::fmod((gf - bf) / d, 6.0);
    else if (mx == gf)
        h = 60.0 * ((bf - rf) / d + 2.0);
    else
        h = 60.0 * ((rf - gf) / d + 4.0);
    h = std::fmod(h, 360.0);
    if (h < 0)
        h += 360.0;
    out.h = float(h);
    return out;
}

void hslToRgbF(float h, float s, float l, int* r, int* g, int* b)
{
    s = qBound(0.f, s, 1.f);
    l = qBound(0.f, l, 1.f);
    if (s <= 1e-6f) {
        const int v = clamp255d(double(l) * 255.0);
        *r = *g = *b = v;
        return;
    }
    // Standard C / X / m conversion.
    const double hd = std::fmod(double(h), 360.0) / 60.0;
    const int i = int(std::floor(hd)) % 6;
    const double f = hd - std::floor(hd);
    const double c = (1.0 - std::fabs(2.0 * double(l) - 1.0)) * double(s);
    const double x = c * (1.0 - std::fabs(2.0 * f - 1.0));
    const double m = double(l) - c * 0.5;
    double rr, gg, bb;
    switch (i) {
    case 0: rr = c; gg = x; bb = 0.0; break;
    case 1: rr = x; gg = c; bb = 0.0; break;
    case 2: rr = 0.0; gg = c; bb = x; break;
    case 3: rr = 0.0; gg = x; bb = c; break;
    case 4: rr = x; gg = 0.0; bb = c; break;
    default: rr = c; gg = 0.0; bb = x; break;
    }
    *r = clamp255d((rr + m) * 255.0);
    *g = clamp255d((gg + m) * 255.0);
    *b = clamp255d((bb + m) * 255.0);
}

// -------------------------------------------------------------- curves
/// Builds a 256 entry lookup table from a flat list of (pos, value) pairs.
void buildCurveLut(const QVector<int>& points, quint8 lut[256])
{
    struct Pt
    {
        int x, y;
    };
    std::vector<Pt> pts;
    pts.reserve(size_t(points.size() / 2) + 2);
    for (int i = 0; i + 1 < points.size(); i += 2) {
        Pt p{ qBound(0, points[i], 255), qBound(0, points[i + 1], 255) };
        if (!pts.empty() && pts.back().x == p.x)
            pts.back().y = p.y;
        else
            pts.push_back(p);
    }
    if (pts.empty()) {
        for (int i = 0; i < 256; ++i)
            lut[i] = quint8(i);
        return;
    }
    std::sort(pts.begin(), pts.end(), [](const Pt& a, const Pt& b) { return a.x < b.x; });
    // Anchor the ends so the curve always spans the whole range.
    if (pts.front().x > 0)
        pts.insert(pts.begin(), Pt{ 0, pts.front().y });
    if (pts.back().x < 255)
        pts.push_back(Pt{ 255, pts.back().y });

    size_t i = 0;
    for (int x = 0; x < 256; ++x) {
        while (i + 1 < pts.size() && pts[i + 1].x <= x)
            ++i;
        if (i + 1 >= pts.size()) {
            lut[x] = quint8(pts[i].y);
            continue;
        }
        const int x0 = pts[i].x, y0 = pts[i].y;
        const int x1 = pts[i + 1].x, y1 = pts[i + 1].y;
        if (x1 == x0) {
            lut[x] = quint8(std::min(y0, y1));
            continue;
        }
        const double t = double(x - x0) / double(x1 - x0);
        lut[x] = quint8(clamp255d(y0 + (y1 - y0) * t));
    }
}

// -------------------------------------------------------------- levels
void buildLevelLut(quint8 lut[256], int inLow, int inHigh, int outLow, int outHigh, double gamma)
{
    inLow = qBound(0, inLow, 254);
    inHigh = qBound(inLow + 1, inHigh, 255);
    outLow = qBound(0, outLow, 255);
    outHigh = qBound(0, outHigh, 255);
    if (outHigh < outLow)
        std::swap(outLow, outHigh);
    gamma = qBound(0.1, gamma, 9.99);
    const double span = inHigh - inLow;
    const double outSpan = outHigh - outLow;
    for (int i = 0; i < 256; ++i) {
        double v = (i - inLow) / span;
        v = qBound(0.0, v, 1.0);
        v = std::pow(v, 1.0 / gamma);
        lut[i] = quint8(clamp255d(v * outSpan + outLow));
    }
}

// -------------------------------------------------------------- ordered dither
const quint8 kBayer4[16] = { 0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5 };

inline double bayerOffset(int x, int y)
{
    return (double(kBayer4[(y & 3) * 4 + (x & 3)]) + 0.5) / 16.0 - 0.5;
}

} // namespace

// ==============================================================
//  Brightness / contrast
// ==============================================================
void brightnessContrast(Surface& s, const Selection& sel, int brightness, int contrast)
{
    if (s.isNull())
        return;
    const double c = qBound(-100.0, double(contrast), 100.0) * 2.55;
    const double k = (259.0 * (c + 255.0)) / (255.0 * (259.0 - c));
    const double b = qBound(-100.0, double(brightness), 100.0) * 2.55;
    if (std::fabs(k - 1.0) < 1e-9 && std::fabs(b) < 1e-9)
        return;

    forEachPixel(s, sel, [k, b](int, int, pixel_t& p) {
        const quint8 a = getA(p);
        if (a == 0)
            return;
        const int r = clamp255d((uR(p) - 128.0) * k + 128.0 + b);
        const int g = clamp255d((uG(p) - 128.0) * k + 128.0 + b);
        const int bl = clamp255d((uB(p) - 128.0) * k + 128.0 + b);
        p = qPremult(a, quint8(r), quint8(g), quint8(bl));
    });
}

void brightness(Surface& s, const Selection& sel, int value)
{
    if (s.isNull())
        return;
    const double b = qBound(-100.0, double(value), 100.0) * 2.55;
    if (std::fabs(b) < 1e-9)
        return;
    forEachPixel(s, sel, [b](int, int, pixel_t& p) {
        const quint8 a = getA(p);
        if (a == 0)
            return;
        const int r = clamp255d(uR(p) + b);
        const int g = clamp255d(uG(p) + b);
        const int bl = clamp255d(uB(p) + b);
        p = qPremult(a, quint8(r), quint8(g), quint8(bl));
    });
}

void contrast(Surface& s, const Selection& sel, int value)
{
    if (s.isNull())
        return;
    const double c = qBound(-100.0, double(value), 100.0) * 2.55;
    const double k = (259.0 * (c + 255.0)) / (255.0 * (259.0 - c));
    if (std::fabs(k - 1.0) < 1e-9)
        return;
    forEachPixel(s, sel, [k](int, int, pixel_t& p) {
        const quint8 a = getA(p);
        if (a == 0)
            return;
        const int r = clamp255d((uR(p) - 128.0) * k + 128.0);
        const int g = clamp255d((uG(p) - 128.0) * k + 128.0);
        const int bl = clamp255d((uB(p) - 128.0) * k + 128.0);
        p = qPremult(a, quint8(r), quint8(g), quint8(bl));
    });
}

// ==============================================================
//  Curves
// ==============================================================
void curves(Surface& s, const Selection& sel, const QVector<int>& rgbPoints, const QVector<int>& redPoints,
            const QVector<int>& greenPoints, const QVector<int>& bluePoints)
{
    if (s.isNull())
        return;
    quint8 lR[256], lG[256], lB[256];
    const bool hasC = !rgbPoints.isEmpty();
    const bool hasCh = !redPoints.isEmpty() || !greenPoints.isEmpty() || !bluePoints.isEmpty();
    if (!hasC && !hasCh)
        return;
    buildCurveLut(hasC ? rgbPoints : QVector<int>(), lR);
    buildCurveLut(hasC ? rgbPoints : QVector<int>(), lG);
    buildCurveLut(hasC ? rgbPoints : QVector<int>(), lB);
    if (hasCh) {
        buildCurveLut(redPoints, lR);
        buildCurveLut(greenPoints, lG);
        buildCurveLut(bluePoints, lB);
    }

    forEachPixel(s, sel, [lR, lG, lB](int, int, pixel_t& p) {
        const quint8 a = getA(p);
        if (a == 0)
            return;
        p = qPremult(a, lR[uR(p)], lG[uG(p)], lB[uB(p)]);
    });
}

// ==============================================================
//  Levels
// ==============================================================
void levels(Surface& s, const Selection& sel, int inputLow, int inputHigh, int outputLow, int outputHigh,
            double gamma, bool useRgb, int redLow, int redHigh, double redGamma, int greenLow, int greenHigh,
            double greenGamma, int blueLow, int blueHigh, double blueGamma)
{
    if (s.isNull())
        return;
    quint8 lR[256], lG[256], lB[256];
    if (useRgb) {
        buildLevelLut(lR, redLow, redHigh, outputLow, outputHigh, redGamma);
        buildLevelLut(lG, greenLow, greenHigh, outputLow, outputHigh, greenGamma);
        buildLevelLut(lB, blueLow, blueHigh, outputLow, outputHigh, blueGamma);
    } else {
        buildLevelLut(lR, inputLow, inputHigh, outputLow, outputHigh, gamma);
        buildLevelLut(lG, inputLow, inputHigh, outputLow, outputHigh, gamma);
        buildLevelLut(lB, inputLow, inputHigh, outputLow, outputHigh, gamma);
    }

    forEachPixel(s, sel, [lR, lG, lB](int, int, pixel_t& p) {
        const quint8 a = getA(p);
        if (a == 0)
            return;
        p = qPremult(a, lR[uR(p)], lG[uG(p)], lB[uB(p)]);
    });
}

// ==============================================================
//  Hue / saturation
// ==============================================================
void hueSaturation(Surface& s, const Selection& sel, int hue, int saturation, int lightness, bool colorize)
{
    if (s.isNull())
        return;
    const double dh = double(hue);
    const double ds = double(saturation) / 100.0;
    const double dl = double(lightness) / 100.0;
    if (!colorize && hue == 0 && saturation == 0 && lightness == 0)
        return;
    const double colorHue = std::fmod(double(hue), 360.0);
    const double colorSat = qBound(0.0, std::fabs(ds), 1.0);

    forEachPixel(s, sel, [&](int, int, pixel_t& p) {
        const quint8 a = getA(p);
        if (a == 0)
            return;
        Hsl c = rgbToHslF(uR(p), uG(p), uB(p));
        if (colorize) {
            c.h = float(colorHue < 0 ? colorHue + 360.0 : colorHue);
            c.s = float(colorSat);
            c.l = qBound(0.f, float(0.5 + dl * 0.5), 1.f);
        } else {
            c.h = float(std::fmod(c.h + dh, 360.0));
            if (c.h < 0)
                c.h += 360.f;
            c.s = qBound(0.f, float(c.s + ds), 1.f);
            if (dl >= 0.0)
                c.l = qBound(0.f, float(c.l + dl * (1.0 - c.l)), 1.f);
            else
                c.l = qBound(0.f, float(c.l * (1.0 + dl)), 1.f);
        }
        int r, g, b;
        hslToRgbF(c.h, c.s, c.l, &r, &g, &b);
        p = qPremult(a, quint8(r), quint8(g), quint8(b));
    });
}

// ==============================================================
//  Colour balance (shadows / midtones / highlights)
// ==============================================================
void colorBalance(Surface& s, const Selection& sel, int shadowsCyanRed, int shadowsMagentaGreen,
                  int shadowsYellowBlue, int midtonesCyanRed, int midtonesMagentaGreen, int midtonesYellowBlue,
                  int highlightsCyanRed, int highlightsMagentaGreen, int highlightsYellowBlue,
                  bool preserveLuminosity)
{
    if (s.isNull())
        return;
    const double d[3][3] = {
        { double(shadowsCyanRed) / 100.0, double(shadowsMagentaGreen) / 100.0, double(shadowsYellowBlue) / 100.0 },
        { double(midtonesCyanRed) / 100.0, double(midtonesMagentaGreen) / 100.0,
          double(midtonesYellowBlue) / 100.0 },
        { double(highlightsCyanRed) / 100.0, double(highlightsMagentaGreen) / 100.0,
          double(highlightsYellowBlue) / 100.0 },
    };

    forEachPixel(s, sel, [d, preserveLuminosity](int, int, pixel_t& p) {
        const quint8 a = getA(p);
        if (a == 0)
            return;
        double c[3] = { double(uR(p)), double(uG(p)), double(uB(p)) };
        const double l = (0.299 * c[0] + 0.587 * c[1] + 0.114 * c[2]) / 255.0;
        // Band weights: shadows ~ (1-l)^2, midtones ~ triangle, highlights ~ l^2
        double w[3];
        w[0] = (1.0 - l) * (1.0 - l);
        w[1] = 4.0 * l * (1.0 - l);
        w[2] = l * l;
        for (int ch = 0; ch < 3; ++ch) {
            double add = 0.0;
            for (int b = 0; b < 3; ++b)
                add += d[b][ch] * w[b];
            c[ch] = qBound(0.0, c[ch] + add * 255.0, 255.0);
        }
        if (preserveLuminosity) {
            const double nl = 0.299 * c[0] + 0.587 * c[1] + 0.114 * c[2];
            const double ol = 0.299 * uR(p) + 0.587 * uG(p) + 0.114 * uB(p);
            if (nl > 0.5) {
                const double k = ol / nl;
                for (int ch = 0; ch < 3; ++ch)
                    c[ch] = qBound(0.0, c[ch] * k, 255.0);
            }
        }
        p = qPremult(a, quint8(clamp255d(c[0])), quint8(clamp255d(c[1])), quint8(clamp255d(c[2])));
    });
}

// ==============================================================
//  Desaturate
// ==============================================================
void desaturate(Surface& s, const Selection& sel, int amount, bool hslWeights, int low, int mid, int high,
                bool switchColors)
{
    if (s.isNull())
        return;
    const double f = pctOrAbs(amount);
    if (f <= 0.0)
        return;
    const double wl = double(low) / 100.0;
    const double wm = double(mid) / 100.0;
    const double wh = double(high) / 100.0;

    forEachPixel(s, sel, [f, hslWeights, wl, wm, wh, switchColors](int, int, pixel_t& p) {
        const quint8 a = getA(p);
        if (a == 0)
            return;
        int r = uR(p), g = uG(p), b = uB(p);
        Hsl c = rgbToHslF(r, g, b);
        double sat = c.s;
        if (hslWeights) {
            // Triangular bands over the lightness axis, normalised.
            const double l = c.l;
            double bl = std::max(0.0, 1.0 - l * 2.0);
            double bm = std::max(0.0, 1.0 - std::fabs(l - 0.5) * 4.0);
            double bh = std::max(0.0, l * 2.0 - 1.0);
            const double sum = bl + bm + bh;
            if (sum > 1e-6) {
                bl /= sum;
                bm /= sum;
                bh /= sum;
            }
            const double weight = wl * bl + wm * bm + wh * bh;
            sat = c.s * (1.0 - f * weight);
        } else {
            sat = c.s * (1.0 - f);
        }
        sat = qBound(0.0, sat, 1.0);
        float h = c.h;
        if (switchColors)
            h = float(std::fmod(h + 180.0, 360.0));
        int nr, ng, nb;
        if (sat <= 1e-6 && !switchColors) {
            nr = ng = nb = clamp255d(c.l * 255.0);
        } else {
            hslToRgbF(h, float(sat), c.l, &nr, &ng, &nb);
        }
        p = qPremult(a, quint8(qBound(0, nr, 255)), quint8(qBound(0, ng, 255)), quint8(qBound(0, nb, 255)));
    });
}

// ==============================================================
//  Invert
// ==============================================================
void invert(Surface& s, const Selection& sel)
{
    if (s.isNull())
        return;
    forEachPixel(s, sel, [](int, int, pixel_t& p) {
        const quint8 a = getA(p);
        if (a == 0)
            return;
        p = qPremult(a, quint8(255 - uR(p)), quint8(255 - uG(p)), quint8(255 - uB(p)));
    });
}

// ==============================================================
//  Posterize
// ==============================================================
void posterize(Surface& s, const Selection& sel, int levels, int dithering)
{
    if (s.isNull())
        return;
    const int lv = qBound(2, levels, 255);
    if (lv >= 255 && dithering <= 0)
        return;
    const double step = 255.0 / double(lv - 1);
    const double dither = qBound(0.0, double(dithering) / 100.0, 1.0) * step * 0.9;

    forEachPixel(s, sel, [step, dither](int x, int y, pixel_t& p) {
        const quint8 a = getA(p);
        if (a == 0)
            return;
        const double off = dither != 0.0 ? bayerOffset(x, y) * dither : 0.0;
        const int r = clamp255d(std::floor((uR(p) + off) / step + 0.5) * step);
        const int g = clamp255d(std::floor((uG(p) + off) / step + 0.5) * step);
        const int b = clamp255d(std::floor((uB(p) + off) / step + 0.5) * step);
        p = qPremult(a, quint8(r), quint8(g), quint8(b));
    });
}

// ==============================================================
//  Threshold
// ==============================================================
void threshold(Surface& s, const Selection& sel, int amount)
{
    if (s.isNull())
        return;
    const int t = int(std::lround(pctOrAbs(amount) * 255.0));
    forEachPixel(s, sel, [t](int, int, pixel_t& p) {
        const quint8 a = getA(p);
        if (a == 0)
            return;
        const int l = luma255(uR(p), uG(p), uB(p));
        const quint8 v = (l >= t) ? 255 : 0;
        p = qPremult(a, v, v, v);
    });
}

// ==============================================================
//  Selective colour
// ==============================================================
void selectiveColor(Surface& s, const Selection& sel, int reds, int yellows, int greens, int cyans, int blues,
                    int magentas, int neutrals, bool relative, int cmykC, int cmykM, int cmykY, int cmykK)
{
    if (s.isNull())
        return;
    // The four CMYK sliders refine the matching colour range.
    double amt[7];
    amt[0] = double(reds);
    amt[1] = double(yellows) + double(cmykY);
    amt[2] = double(greens);
    amt[3] = double(cyans) + double(cmykC);
    amt[4] = double(blues);
    amt[5] = double(magentas) + double(cmykM);
    amt[6] = double(neutrals) + double(cmykK);
    bool any = false;
    for (double v : amt)
        any = any || std::fabs(v) > 1e-9;
    if (!any)
        return;

    forEachPixel(s, sel, [amt, relative](int, int, pixel_t& p) {
        const quint8 a = getA(p);
        if (a == 0)
            return;
        const int ri = uR(p), gi = uG(p), bi = uB(p);
        Hsl hsl = rgbToHslF(ri, gi, bi);
        const double h = hsl.h;
        const double sat = std::max(0.0, double(hsl.s));
        const double chroma = std::min(1.0, sat * 2.2);
        const double gray = 1.0 - chroma;

        // Six 60 degree hue windows, centred on 0/60/120/180/240/300.
        double w[6];
        for (int i = 0; i < 6; ++i) {
            double diff = std::fmod(std::fabs(h - double(i) * 60.0), 360.0);
            if (diff > 180.0)
                diff = 360.0 - diff;
            w[i] = std::max(0.0, 1.0 - diff / 60.0) * chroma;
        }
        const double wn = gray;

        // reds, yellows, greens, cyans, blues, magentas
        const double dC = (-amt[0] * w[0] + amt[3] * w[3]) / 100.0;
        const double dM = (-amt[2] * w[2] + amt[5] * w[5]) / 100.0;
        const double dY = (-amt[4] * w[4] + amt[1] * w[1]) / 100.0;
        const double dK = (-amt[6] * wn) / 100.0;

        double C = 1.0 - ri / 255.0;
        double M = 1.0 - gi / 255.0;
        double Y = 1.0 - bi / 255.0;
        double K = std::min(C, std::min(M, Y));
        C -= K;
        M -= K;
        Y -= K;

        if (relative) {
            C = qBound(0.0, C * (1.0 + dC), 1.0);
            M = qBound(0.0, M * (1.0 + dM), 1.0);
            Y = qBound(0.0, Y * (1.0 + dY), 1.0);
            K = qBound(0.0, K * (1.0 + dK), 1.0);
        } else {
            C = qBound(0.0, C + dC, 1.0);
            M = qBound(0.0, M + dM, 1.0);
            Y = qBound(0.0, Y + dY, 1.0);
            K = qBound(0.0, K + dK, 1.0);
        }
        C = qBound(0.0, std::min(1.0, C + K), 1.0);
        M = qBound(0.0, std::min(1.0, M + K), 1.0);
        Y = qBound(0.0, std::min(1.0, Y + K), 1.0);
        const int nr = clamp255d((1.0 - C) * 255.0);
        const int ng = clamp255d((1.0 - M) * 255.0);
        const int nb = clamp255d((1.0 - Y) * 255.0);
        p = qPremult(a, quint8(nr), quint8(ng), quint8(nb));
    });
}

// ==============================================================
//  Temperature / tint
// ==============================================================
void temperatureTint(Surface& s, const Selection& sel, int temperature, int tint)
{
    if (s.isNull())
        return;
    const double t = qBound(-100.0, double(temperature), 100.0) / 100.0;
    const double ti = qBound(-100.0, double(tint), 100.0) / 100.0;
    if (std::fabs(t) < 1e-9 && std::fabs(ti) < 1e-9)
        return;

    forEachPixel(s, sel, [t, ti](int, int, pixel_t& p) {
        const quint8 a = getA(p);
        if (a == 0)
            return;
        Hsl c = rgbToHslF(uR(p), uG(p), uB(p));
        // The stronger the colour, the stronger the shift.
        const double k = (0.35 + 0.65 * double(c.s)) * 110.0;
        double r = uR(p), g = uG(p), b = uB(p);
        r += t * k;
        b -= t * k;
        g -= ti * k;
        r += ti * k * 0.65;
        b += ti * k * 0.65;
        p = qPremult(a, quint8(clamp255d(r)), quint8(clamp255d(g)), quint8(clamp255d(b)));
    });
}

// ==============================================================
//  Replace colour
// ==============================================================
void replaceColor(Surface& s, const Selection& sel, pixel_t from, pixel_t to, int tolerance, bool useHue,
                  int hueShift, int saturation, int lightness)
{
    if (s.isNull())
        return;
    const int fr = uR(from), fg = uG(from), fb = uB(from);
    const int toR = uR(to), toG = uG(to), toB = uB(to);
    const int tol = qMax(1, tolerance);
    const Hsl fromHsl = rgbToHslF(fr, fg, fb);
    const bool srcIsGray = fromHsl.s < 0.08f;

    forEachPixel(s, sel, [&](int, int, pixel_t& p) {
        const quint8 a = getA(p);
        if (a == 0)
            return;
        const int r = uR(p), g = uG(p), b = uB(p);
        double dist;
        Hsl c = rgbToHslF(r, g, b);
        if (useHue && !srcIsGray) {
            if (c.s < 0.06f)
                return; // neutral pixels have no meaningful hue
            double diff = std::fmod(std::fabs(c.h - fromHsl.h), 360.0);
            if (diff > 180.0)
                diff = 360.0 - diff;
            dist = diff * 255.0 / 180.0;
        } else {
            const double dr = r - fr, dg = g - fg, db = b - fb;
            dist = std::sqrt((3.0 * dr * dr + 4.0 * dg * dg + 2.0 * db * db) / 9.0);
        }
        if (dist >= tol)
            return;
        const double f = 1.0 - dist / double(tol);
        int nr = r, ng = g, nb = b;
        if (useHue) {
            Hsl o = c;
            o.h = float(std::fmod(o.h + double(hueShift), 360.0));
            if (o.h < 0)
                o.h += 360.f;
            o.s = qBound(0.f, float(o.s + double(saturation) / 100.0), 1.f);
            if (lightness >= 0)
                o.l = qBound(0.f, float(o.l + double(lightness) / 200.0), 1.f);
            else
                o.l = qBound(0.f, float(o.l * (1.0 + double(lightness) / 100.0)), 1.f);
            hslToRgbF(o.h, o.s, o.l, &nr, &ng, &nb);
        }
        nr = int(nr + (toR - nr) * f + 0.5);
        ng = int(ng + (toG - ng) * f + 0.5);
        nb = int(nb + (toB - nb) * f + 0.5);
        p = qPremult(a, quint8(clamp255d(nr)), quint8(clamp255d(ng)), quint8(clamp255d(nb)));
    });
}

// ==============================================================
//  Channel mixer
// ==============================================================
void channelMixer(Surface& s, const Selection& sel, int rr, int rg, int rb, int gr, int gg, int gb, int br,
                  int bg, int bb, bool monochrome, int mr, int mg, int mb)
{
    if (s.isNull())
        return;
    forEachPixel(s, sel, [=](int, int, pixel_t& p) {
        const quint8 a = getA(p);
        if (a == 0)
            return;
        const int r = uR(p), g = uG(p), b = uB(p);
        double nr = (rr * r + rg * g + rb * b) / 100.0;
        double ng = (gr * r + gg * g + gb * b) / 100.0;
        double nbl = (br * r + bg * g + bb * b) / 100.0;
        if (monochrome) {
            const double l = 0.299 * nr + 0.587 * ng + 0.114 * nbl;
            nr = ng = nbl = l;
        }
        nr += mr;
        ng += mg;
        nbl += mb;
        p = qPremult(a, quint8(clamp255d(nr)), quint8(clamp255d(ng)), quint8(clamp255d(nbl)));
    });
}

// ==============================================================
//  Auto levels
// ==============================================================
void autoAdjustLevels(Surface& s, const Selection& sel)
{
    if (s.isNull())
        return;
    const int w = s.width(), h = s.height();
    if (w <= 0 || h <= 0)
        return;

    const QImage* mask = nullptr;
    if (!sel.isNull() && sel.width() == w && sel.height() == h)
        mask = &sel.mask();

    long long hist[3][256] = { { 0 } };
    long long total = 0;
    for (int y = 0; y < h; ++y) {
        const pixel_t* row = s.scanLine(y);
        const uchar* mrow = mask ? mask->constScanLine(y) : nullptr;
        for (int x = 0; x < w; ++x) {
            if (mrow && mrow[x] == 0)
                continue;
            const pixel_t p = row[x];
            if (getA(p) == 0)
                continue;
            ++hist[0][uR(p)];
            ++hist[1][uG(p)];
            ++hist[2][uB(p)];
            ++total;
        }
    }
    if (total < 2)
        return;

    // Ignore fully black / fully white pixels: they are usually clipped.
    const long long clip = std::max<long long>(1, total / 1000);
    int lo[3], hi[3];
    for (int c = 0; c < 3; ++c) {
        long long acc = 0;
        lo[c] = 0;
        hi[c] = 255;
        for (int v = 1; v < 255; ++v) {
            acc += hist[c][v];
            if (acc >= clip) {
                lo[c] = v;
                break;
            }
        }
        acc = 0;
        for (int v = 254; v > 0; --v) {
            acc += hist[c][v];
            if (acc >= clip) {
                hi[c] = v;
                break;
            }
        }
        if (hi[c] - lo[c] < 4) {
            lo[c] = 0;
            hi[c] = 255;
        }
    }
    if (lo[0] == 0 && hi[0] == 255 && lo[1] == 0 && hi[1] == 255 && lo[2] == 0 && hi[2] == 255)
        return;

    quint8 lR[256], lG[256], lB[256];
    buildLevelLut(lR, lo[0], hi[0], 0, 255, 1.0);
    buildLevelLut(lG, lo[1], hi[1], 0, 255, 1.0);
    buildLevelLut(lB, lo[2], hi[2], 0, 255, 1.0);
    for (int c = 0; c < 3; ++c)
        for (int v = 0; v < 256; ++v) {
            if (v < lo[c] || v > hi[c]) {
                quint8* lut = (c == 0) ? lR : (c == 1 ? lG : lB);
                lut[v] = (v <= lo[c]) ? 0 : 255;
            }
        }

    forEachPixel(s, sel, [lR, lG, lB](int, int, pixel_t& p) {
        const quint8 a = getA(p);
        if (a == 0)
            return;
        p = qPremult(a, lR[uR(p)], lG[uG(p)], lB[uB(p)]);
    });
}

} // namespace Effects
} // namespace pnq
