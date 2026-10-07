#include "core/ColorUtils.h"

#include <QHash>
#include <QtMath>

#include <algorithm>
#include <cmath>

namespace pnq {

namespace {

inline int clamp255(double v)
{
    int i = int(v + 0.5);
    return i < 0 ? 0 : (i > 255 ? 255 : i);
}

inline int mul255(int a, int b) { return (a * b + 127) / 255; }

struct Rgb { int r, g, b; };

float lum(const Rgb& c)
{
    return 0.3f * c.r + 0.59f * c.g + 0.11f * c.b;
}

Rgb clipColor(Rgb c)
{
    float l = lum(c);
    float n = std::min(c.r, std::min(c.g, c.b));
    float x = std::max(c.r, std::max(c.g, c.b));
    if (n < 0.0f) {
        c.r = l + (c.r - l) * l / (l - n);
        c.g = l + (c.g - l) * l / (l - n);
        c.b = l + (c.b - l) * l / (l - n);
    }
    if (x > 255.0f) {
        c.r = l + (c.r - l) * (255.0f - l) / (x - l);
        c.g = l + (c.g - l) * (255.0f - l) / (x - l);
        c.b = l + (c.b - l) * (255.0f - l) / (x - l);
    }
    return c;
}

void setLum(Rgb* c, float l)
{
    float d = l - lum(*c);
    c->r += d;
    c->g += d;
    c->b += d;
    *c = clipColor(*c);
}

float sat(const Rgb& c)
{
    return std::max(c.r, std::max(c.g, c.b)) - std::min(c.r, std::min(c.g, c.b));
}

void setSat(Rgb* c, float s)
{
    int mx = std::max(c->r, std::max(c->g, c->b));
    int mn = std::min(c->r, std::min(c->g, c->b));
    if (mx > mn) {
        c->r = int((c->r - mn) * s / float(mx - mn));
        c->g = int((c->g - mn) * s / float(mx - mn));
        c->b = int((c->b - mn) * s / float(mx - mn));
    } else {
        c->r = c->g = c->b = 0;
    }
    *c = clipColor(*c);
}

int divRound(int a, int b)
{
    return (a + b / 2) / b;
}

// HSL helpers (for hue/sat adjustments)
void rgbToHslF(int r, int g, int b, float* h, float* s, float* l)
{
    float rf = r / 255.0f, gf = g / 255.0f, bf = b / 255.0f;
    float mx = std::max(rf, std::max(gf, bf));
    float mn = std::min(rf, std::min(gf, bf));
    *l = (mx + mn) * 0.5f;
    if (mx == mn) {
        *h = 0.0f;
        *s = 0.0f;
        return;
    }
    float d = mx - mn;
    *s = (*l > 0.5f) ? d / (2.0f - mx - mn) : d / (mx + mn);
    if (mx == rf)
        *h = (gf - bf) / d + (gf < bf ? 6.0f : 0.0f);
    else if (mx == gf)
        *h = (bf - rf) / d + 2.0f;
    else
        *h = (rf - gf) / d + 4.0f;
    *h /= 6.0f;
}

void hslToRgbF(float h, float s, float l, int* r, int* g, int* b)
{
    if (s == 0.0f) {
        *r = *g = *b = clamp255(l * 255.0f);
        return;
    }
    float q = (l < 0.5f) ? l * (1.0f + s) : l + s - l * s;
    float p = 2.0f * l - q;
    float t[3];
    float hs = (h < 0.0f) ? h + 1.0f : (h > 1.0f ? h - 1.0f : h);
    t[0] = hs + 1.0f / 3.0f;
    t[1] = hs;
    t[2] = hs - 1.0f / 3.0f;
    for (int i = 0; i < 3; ++i) {
        if (t[i] < 0.0f) t[i] += 1.0f;
        if (t[i] > 1.0f) t[i] -= 1.0f;
    }
    auto ramp = [](float tt) {
        if (tt < 0.0f) tt += 1.0f;
        if (tt > 1.0f) tt -= 1.0f;
        return tt;
    };
    float rr = ramp(t[0]), gg = ramp(t[1]), bb = ramp(t[2]);
    auto mix = [&](float tt) {
        if (tt < 0.0f) tt += 1.0f;
        if (tt > 1.0f) tt -= 1.0f;
        if (tt < 1.0f / 6.0f) return p + (q - p) * 6.0f * tt;
        if (tt < 0.5f) return q;
        if (tt < 2.0f / 3.0f) return p + (q - p) * (2.0f / 3.0f - tt) * 6.0f;
        return p;
    };
    *r = clamp255(mix(rr) * 255.0f);
    *g = clamp255(mix(gg) * 255.0f);
    *b = clamp255(mix(bb) * 255.0f);
}

} // namespace

pixel_t qPremult(quint8 a, quint8 r, quint8 g, quint8 b)
{
    if (a == 255)
        return (quint32(255) << 24) | (quint32(r) << 16) | (quint32(g) << 8) | quint32(b);
    if (a == 0)
        return 0;
    return (quint32(a) << 24) | (quint32(mul255(r, a)) << 16) | (quint32(mul255(g, a)) << 8)
           | quint32(mul255(b, a));
}

quint8 blendChannel(BlendMode mode, int cb, int cs, quint8 cbA, quint8 csA)
{
    // cbA/csA are used by the color-dodge/burn variants that reference alpha.
    Q_UNUSED(cbA);
    Q_UNUSED(csA);
    switch (mode) {
    case BlendMode::Multiply: return quint8(mul255(cb, cs));
    case BlendMode::Screen: return quint8(cb + cs - mul255(cb, cs));
    case BlendMode::Overlay: {
        if (cb < 128) return quint8(2 * cb * cs / 255);
        int x = 2 * cb - 255;
        return quint8(clamp255(cs + x - cs * x / 255));
    }
    case BlendMode::HardLight: {
        if (cs < 128) return quint8(2 * cs * cb / 255);
        int x = 2 * cs - 255;
        return quint8(clamp255(cb + x - cb * x / 255));
    }
    case BlendMode::SoftLight: {
        double b = cb, s = cs;
        double r;
        if (s <= 0.5)
            r = b - (1.0 - 2.0 * s) * b * (1.0 - b);
        else if (b <= 0.25)
            r = ((16.0 * b - 12.0) * b + 4.0) * b;
        else
            r = std::sqrt(b);
        return quint8(clamp255(r));
    }
    case BlendMode::Darken: return quint8(std::min(cb, cs));
    case BlendMode::Lighten: return quint8(std::max(cb, cs));
    case BlendMode::ColorDodge:
        // Paint.NET checks only the overlay: 255 dodges to 255 even over black.
        // An early return for a black backdrop here hid whole channels.
        if (cs == 255) return 255;
        return quint8(std::min(255, cb * 255 / (255 - cs)));
    case BlendMode::ColorBurn:
        if (cb == 255) return 255;
        if (cs == 0) return 0;
        return quint8(std::max(0, 255 - (255 - cb) * 255 / cs));
    case BlendMode::LinearDodge: return quint8(std::min(255, cb + cs));
    case BlendMode::LinearBurn: return quint8(std::max(0, cb + cs - 255));
    case BlendMode::VividLight: {
        // ColorBurn(cb, 2*cs) when cs <= 0.5, else ColorDodge(cb, 2*cs-1)
        if (cs <= 127) {
            int c2 = 2 * cs;
            if (cb == 255) return 255;
            if (c2 == 0) return 0;
            return quint8(std::max(0, 255 - (255 - cb) * 255 / c2));
        }
        int c2 = 2 * (cs - 128);
        if (cb == 0) return 0;
        return quint8(std::min(255, cb * 255 / qMax(1, 255 - c2)));
    }
    case BlendMode::LinearLight: return quint8(clamp255(cb + 2 * cs - 255));
    case BlendMode::PinLight:
        if (cs < 128) return quint8(std::min(cb, 2 * cs));
        return quint8(std::max(cb, 2 * (cs - 128)));
    case BlendMode::HardMix: {
        double v = cb + 2.0 * cs - 255.0;
        return v >= 127.5 ? 255 : 0;
    }
    case BlendMode::Difference: return quint8(std::abs(cb - cs));
    case BlendMode::Exclusion: return quint8(clamp255(cb + cs - 2 * cb * cs / 255));
    case BlendMode::Negation: return quint8(255 - std::abs(255 - cb - cs));
    case BlendMode::Xor: return quint8(cb ^ cs);
    case BlendMode::Reflect: {
        // Paint.NET: A*A/(1-B), B == 1 gives 1.
        if (cs == 255)
            return 255;
        const int den = 255 - cs;
        return quint8(std::min(255, (cb * cb + den / 2) / den));
    }
    case BlendMode::Glow: {
        // Paint.NET: B*B/(1-A), A == 1 gives 1.
        if (cb == 255)
            return 255;
        const int den = 255 - cb;
        return quint8(std::min(255, (cs * cs + den / 2) / den));
    }
    case BlendMode::Subtract: return quint8(std::max(0, cb - cs));
    case BlendMode::Divide: return cs == 0 ? 255 : quint8(clamp255(cb * 255.0 / cs));
    default: return quint8(qBound(0, cb, 255));
    }
}

void blendNonSeparable(BlendMode mode, int cbR, int cbG, int cbB, int csR, int csG, int csB,
                       int* outR, int* outG, int* outB)
{
    switch (mode) {
    case BlendMode::Hue: {
        Rgb c{ cbR, cbG, cbB };
        Rgb s{ csR, csG, csB };
        setSat(&c, sat(s));
        setLum(&c, lum(c));
        *outR = clamp255(c.r);
        *outG = clamp255(c.g);
        *outB = clamp255(c.b);
        break;
    }
    case BlendMode::Saturation: {
        Rgb c{ cbR, cbG, cbB };
        Rgb s{ csR, csG, csB };
        setSat(&c, sat(s));
        setLum(&c, lum(s));
        *outR = clamp255(c.r);
        *outG = clamp255(c.g);
        *outB = clamp255(c.b);
        break;
    }
    case BlendMode::Color: {
        Rgb c{ cbR, cbG, cbB };
        Rgb s{ csR, csG, csB };
        setLum(&c, lum(s));
        *outR = clamp255(c.r);
        *outG = clamp255(c.g);
        *outB = clamp255(c.b);
        break;
    }
    case BlendMode::Luminosity: {
        Rgb c{ cbR, cbG, cbB };
        Rgb s{ csR, csG, csB };
        setLum(&c, lum(s));
        *outR = clamp255(c.r);
        *outG = clamp255(c.g);
        *outB = clamp255(c.b);
        break;
    }
    default:
        *outR = cbR;
        *outG = cbG;
        *outB = cbB;
        break;
    }
}

namespace {
inline bool isNonSeparable(BlendMode m)
{
    return m == BlendMode::Hue || m == BlendMode::Saturation || m == BlendMode::Color
           || m == BlendMode::Luminosity;
}
} // namespace

pixel_t composePixel(pixel_t dst, pixel_t src, BlendMode mode, quint8 layerOpacity)
{
    quint8 sa = getA(src);
    if (layerOpacity != 255)
        sa = quint8(sa * layerOpacity / 255);
    if (sa == 0)
        return dst;
    quint8 da = getA(dst);
    if (da == 0) {
        // Fast path: dst is empty.
        quint8 r = getR(src), g = getG(src), b = getB(src);
        return qPremult(sa, r, g, b);
    }

    switch (mode) {
    case BlendMode::Normal:
    case BlendMode::LegacyNormal: {
        quint8 inv = 255 - sa;
        quint8 r = quint8(mul255(getR(src), sa) + mul255(getR(dst), inv));
        quint8 g = quint8(mul255(getG(src), sa) + mul255(getG(dst), inv));
        quint8 b = quint8(mul255(getB(src), sa) + mul255(getB(dst), inv));
        quint8 a = quint8(sa + mul255(da, inv));
        return qPremult(a, r, g, b);
    }
    case BlendMode::Dissolve: {
        // Stochastic: keep the src pixel with probability sa/255.
        quint32 h = (quint32(src) * 2654435761u) ^ (quint32(dst) * 2246822519u);
        if ((h & 0xFF) < sa)
            return src;
        return dst;
    }
    default:
        break;
    }

    // General W3C compositing formula.
    const float as = sa / 255.0f;
    const float ab = da / 255.0f;
    int br = getR(dst), bg = getG(dst), bb = getB(dst);
    int sr = getR(src), sg = getG(src), sb = getB(src);
    int fr = 0, fg = 0, fb = 0;
    if (isNonSeparable(mode)) {
        blendNonSeparable(mode, br, bg, bb, sr, sg, sb, &fr, &fg, &fb);
    } else if (mode == BlendMode::DarkerColor || mode == BlendMode::LighterColor) {
        int ss = sr + sg + sb;
        int ds = br + bg + bb;
        bool takeSrc = (mode == BlendMode::DarkerColor) ? (ss <= ds) : (ss >= ds);
        fr = takeSrc ? sr : br;
        fg = takeSrc ? sg : bg;
        fb = takeSrc ? sb : bb;
    } else {
        fr = blendChannel(mode, br, sr, da, sa);
        fg = blendChannel(mode, bg, sg, da, sa);
        fb = blendChannel(mode, bb, sb, da, sa);
    }
    float k1 = as * (1.0f - ab);
    float k2 = as * ab;
    float k3 = (1.0f - as) * ab;
    quint8 r = quint8(clamp255(k1 * sr + k2 * fr + k3 * br));
    quint8 g = quint8(clamp255(k1 * sg + k2 * fg + k3 * bg));
    quint8 b = quint8(clamp255(k1 * sb + k2 * fb + k3 * bb));
    quint8 a = quint8(sa + mul255(da, 255 - sa));
    return qPremult(a, r, g, b);
}

// ---------------------------------------------------------------- HSV / HSL

void rgbToHsvInt(int r, int g, int b, int* h, int* s, int* v)
{
    const int mx = qMax(r, qMax(g, b));
    const int mn = qMin(r, qMin(g, b));
    const int d = mx - mn;
    *v = mx;
    *s = mx == 0 ? 0 : (d * 255 + mx / 2) / mx;
    if (d == 0) {
        *h = 0;
        return;
    }
    // Sector base (0, 2 or 4 sixty-degree steps) plus the position inside the
    // sector, both expressed in 1/255 of a full turn so the maths stays integral.
    int sector = 0;
    int f = 0;
    if (mx == r) {
        sector = 0;
        f = (g - b) * 255 / d;
    } else if (mx == g) {
        sector = 2;
        f = (b - r) * 255 / d;
    } else {
        sector = 4;
        f = (r - g) * 255 / d;
    }
    int turns = sector * 255 + f; // 0 .. 5*255 (+/- one sector)
    turns %= 6 * 255;
    if (turns < 0)
        turns += 6 * 255;
    // `turns` counts 1/255 of a sector; one sector is 60 degrees.
    *h = turns * 60 / 255;
}

void hsvToRgbInt(int h, int s, int v, int* r, int* g, int* b)
{
    h = ((h % 360) + 360) % 360;
    s = qBound(0, s, 255);
    v = qBound(0, v, 255);
    const int i = h / 60;
    const int f = h % 60;
    const int p = v * (255 - s) / 255;                  ///< the zeroed channel
    const int t = v * (255 * 60 - s * (60 - f)) / (255 * 60); ///< rising towards i+1
    const int u = v * (255 * 60 - s * f) / (255 * 60);       ///< falling towards i-1
    switch (i) {
    case 0: *r = v; *g = t; *b = p; break; // red   -> yellow
    case 1: *r = u; *g = v; *b = p; break; // yellow-> green
    case 2: *r = p; *g = v; *b = t; break; // green -> cyan
    case 3: *r = p; *g = u; *b = v; break; // cyan  -> blue
    case 4: *r = t; *g = p; *b = v; break; // blue  -> magenta
    default: *r = v; *g = p; *b = u; break; // magenta-> red
    }
}

QColor rgbToHsv(QColor c)
{
    int h, s, v;
    rgbToHsvInt(c.red(), c.green(), c.blue(), &h, &s, &v);
    return QColor::fromHsv(h, s, v, c.alpha());
}

QColor hsvToRgb(QColor c)
{
    int r, g, b;
    hsvToRgbInt(qRound(c.hsvHueF() * 360.0), qRound(c.hsvSaturationF() * 255.0),
                qRound(c.valueF() * 255.0), &r, &g, &b);
    return QColor(r, g, b, c.alpha());
}

QColor rgbToHsl(QColor c)
{
    float h, s, l;
    rgbToHslF(c.red(), c.green(), c.blue(), &h, &s, &l);
    return QColor::fromHslF(h, s, l, c.alphaF());
}

QColor hslToRgb(QColor c)
{
    int r, g, b;
    hslToRgbF(c.hueF(), c.lightnessF(), c.saturationF(), &r, &g, &b);
    return QColor(r, g, b, c.alpha());
}

QColor shiftHue(QColor c, int shift)
{
    float h, s, l;
    rgbToHslF(c.red(), c.green(), c.blue(), &h, &s, &l);
    h = h + shift / 360.0f;
    int r, g, b;
    hslToRgbF(h, s, l, &r, &g, &b);
    return QColor(r, g, b, c.alpha());
}

QColor shiftSaturation(QColor c, int shift)
{
    float h, s, l;
    rgbToHslF(c.red(), c.green(), c.blue(), &h, &s, &l);
    s = std::max(0.0f, std::min(1.0f, s + shift / 100.0f));
    int r, g, b;
    hslToRgbF(h, s, l, &r, &g, &b);
    return QColor(r, g, b, c.alpha());
}

QColor shiftLightness(QColor c, int shift)
{
    float h, s, l;
    rgbToHslF(c.red(), c.green(), c.blue(), &h, &s, &l);
    l = std::max(0.0f, std::min(1.0f, l + shift / 100.0f));
    int r, g, b;
    hslToRgbF(h, s, l, &r, &g, &b);
    return QColor(r, g, b, c.alpha());
}

QColor shiftTemperature(QColor c, int shift)
{
    // Positive -> warmer (more red / less blue)
    int r = qBound(0, c.red() + shift * 2, 255);
    int g = c.green();
    int b = qBound(0, c.blue() - shift * 2, 255);
    return QColor(r, g, b, c.alpha());
}

QColor shiftTint(QColor c, int shift)
{
    int r = c.red();
    int g = qBound(0, c.green() + shift * 2, 255);
    int b = qBound(0, c.blue() + shift * 2, 255);
    return QColor(r, g, b, c.alpha());
}

int colorDistance(pixel_t a, pixel_t b)
{
    int dr = int(getR(a)) - int(getR(b));
    int dg = int(getG(a)) - int(getG(b));
    int db = int(getB(a)) - int(getB(b));
    int da = int(getA(a)) - int(getA(b));
    return qRound(std::sqrt(double(dr * dr * 3 + dg * dg * 4 + db * db * 2 + da * da)));
}

QString colorToHex(pixel_t p, bool withAlpha)
{
    if (withAlpha)
        return QStringLiteral("#%1%2%3%4")
            .arg(getA(p), 2, 16, QLatin1Char('0'))
            .arg(getR(p), 2, 16, QLatin1Char('0'))
            .arg(getG(p), 2, 16, QLatin1Char('0'))
            .arg(getB(p), 2, 16, QLatin1Char('0'));
    return QStringLiteral("#%1%2%3")
        .arg(getR(p), 2, 16, QLatin1Char('0'))
        .arg(getG(p), 2, 16, QLatin1Char('0'))
        .arg(getB(p), 2, 16, QLatin1Char('0'));
}

pixel_t colorFromHex(const QString& s, bool* ok)
{
    QString t = s.trimmed();
    if (!t.startsWith(QLatin1Char('#')))
        t.prepend(QLatin1Char('#'));
    if (ok)
        *ok = false;
    int a = 255;
    if (t.size() == 9) { // #AARRGGBB
        bool o1 = false;
        a = t.mid(1, 2).toInt(&o1, 16);
        if (!o1) return 0;
        t = QLatin1Char('#') + t.mid(3);
    } else if (t.size() == 5) { // #ARGBB
        bool o1 = false;
        a = t.mid(1, 1).toInt(&o1, 16);
        if (!o1) return 0;
        a = a * 17;
        t = QLatin1Char('#') + t.mid(2);
    }
    if (t.size() != 7)
        return 0;
    QColor c(t);
    if (!c.isValid())
        return 0;
    if (ok)
        *ok = true;
    return toPixel(c);
}

QString colorToHsvString(pixel_t p)
{
    int h, s, v;
    rgbToHsvInt(getR(p), getG(p), getB(p), &h, &s, &v);
    return QStringLiteral("H: %1 S: %2 V: %3").arg(h).arg(s).arg(v);
}

QString colorName(pixel_t p)
{
    return colorName(getR(p), getG(p), getB(p));
}

QString colorName(quint8 r, quint8 g, quint8 b)
{
    static QHash<quint32, QString>* names = [] {
        auto* h = new QHash<quint32, QString>();
        struct { const char* n; unsigned v; } tbl[] = {
            { "Black", 0x000000 }, { "Maroon", 0x800000 }, { "Green", 0x008000 },
            { "Olive", 0x808000 }, { "Purple", 0x800080 }, { "Teal", 0x008080 },
            { "Lime", 0x00FF00 }, { "Green Yellow", 0x80FF00 }, { "Cyan", 0x00FFFF },
            { "Light Cyan", 0x80FFFF }, { "Blue", 0x0000FF }, { "Light Blue", 0x8080FF },
            { "Navy", 0x000080 }, { "Dark Blue", 0x000080 }, { "Yellow", 0xFFFF00 },
            { "Light Yellow", 0xFFFF80 }, { "Red", 0xFF0000 }, { "Light Red", 0xFF8080 },
            { "Magenta", 0xFF00FF }, { "Light Magenta", 0xFF80FF }, { "Orange", 0xFFA500 },
            { "Midnight Blue", 0x191970 }, { "Dark Slate Blue", 0x483D8B },
            { "Dark Violet", 0x9400D3 }, { "Dark Orchid", 0x9932CC }, { "Medium Orchid", 0xBA55D3 },
            { "Thistle", 0xD8BFD8 }, { "Pink", 0xFFC0CB }, { "Light Pink", 0xFFB6C1 },
            { "Hot Pink", 0xFF69B4 }, { "Deep Pink", 0xFF1493 }, { "Pale Violet Red", 0xDB7093 },
            { "Medium Purple", 0x9370DB }, { "Blue Violet", 0x8A2BE2 }, { "Dark Slate Gray", 0x2F4F4F },
            { "Light Slate Gray", 0x778899 }, { "Slate Gray", 0x708090 }, { "Dim Gray", 0x696969 },
            { "Light Gray", 0xD3D3D3 }, { "Light Steel Blue", 0xB0C4DE }, { "Steel Blue", 0x4682B4 },
            { "Royal Blue", 0x4169E1 }, { "Medium Blue", 0x0000CD }, { "Dark Blue", 0x00008B },
            { "Cornflower Blue", 0x6495ED }, { "Dodger Blue", 0x1E90FF }, { "Deep Sky Blue", 0x00BFFF },
            { "Sky Blue", 0x87CEEB }, { "Light Sky Blue", 0x87CEFA }, { "Cadet Blue", 0x5F9EA0 },
            { "Dark Cyan", 0x008B8B }, { "Dark Sea Green", 0x8FBC8F }, { "Sea Green", 0x2E8B57 },
            { "Medium Sea Green", 0x3CB371 }, { "Forest Green", 0x228B22 }, { "Dark Green", 0x006400 },
            { "Lime Green", 0x32CD32 }, { "Yellow Green", 0x9ACD32 }, { "Olive Drab", 0x6B8E23 },
            { "Dark Olive Green", 0x556B2F }, { "Dark Goldenrod", 0xB8860B }, { "Goldenrod", 0xDAA520 },
            { "Saddle Brown", 0x8B4513 }, { "Sienna", 0xA0522D }, { "Chocolate", 0xD2691E },
            { "Peru", 0xCD853F }, { "Sandy Brown", 0xF4A460 }, { "Burly Wood", 0xDEB887 },
            { "Wheat", 0xF5DEB3 }, { "Tan", 0xD2B48C }, { "Rosy Brown", 0xBC8F8F },
            { "Indian Red", 0xCD5C5C }, { "Firebrick", 0xB22222 }, { "Dark Red", 0x8B0000 },
            { "Crimson", 0xDC143C }, { "Tomato", 0xFF6347 }, { "Coral", 0xFF7F50 },
            { "Salmon", 0xFA8072 }, { "Light Salmon", 0xFFA07A }, { "Light Coral", 0xF08080 },
            { "Misty Rose", 0xFFE4E1 }, { "Lavender", 0xE6E6FA }, { "Alice Blue", 0xF0F8FF },
            { "Ghost White", 0xF8F8FF }, { "White Smoke", 0xF5F5F5 }, { "Snow", 0xFFFAFA },
            { "White", 0xFFFFFF }, { "Gray", 0x808080 }, { "Dark Gray", 0xA9A9A9 },
            { "Silver", 0xC0C0C0 }, { "Azure", 0xF0FFFF }, { "Aquamarine", 0x7FFFD4 },
            { "Turquoise", 0x40E0D0 }, { "Medium Turquoise", 0x48D1CC },
            { "Light Sea Green", 0x20B2AA }, { "Medium Spring Green", 0x00FA9A },
            { "Spring Green", 0x00FF7F }, { "Medium Aquamarine", 0x66CDAA },
            { "Dark Sea Green ", 0x8FBC8F }, { "Light Goldenrod", 0xFAFAD2 },
            { "Khaki", 0xF0E68C }, { "Pale Goldenrod", 0xEEE8AA }, { "Beige", 0xF5F5DC },
            { "Antique White", 0xFAEBD7 }, { "Floral White", 0xFFFAF0 },
            { "Papaya Whip", 0xFFEFD5 }, { "Blanched Almond", 0xFFEBCD },
            { "Dark Salmon", 0xE9967A }, { "Rosy Brown ", 0xBC8F8F },
            { "Light Goldenrod Yellow", 0xFAFAD2 }, { "Medium Purple ", 0x9370DB },
        };
        for (auto& e : tbl)
            h->insert(e.v, QString::fromLatin1(e.n));
        return h;
    }();
    return names->value((quint32(r) << 16) | (quint32(g) << 8) | b, QString());
}

} // namespace pnq
