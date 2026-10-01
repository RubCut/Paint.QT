#pragma once

#include "core/BlendMode.h"
#include "core/Surface.h"

#include <QColor>
#include <QString>

namespace pnq {

// ---------------------------------------------------------------- channel access
inline quint8 chanA(pixel_t p) { return quint8(p >> 24); }
inline quint8 chanR(pixel_t p) { return quint8(p >> 16); }
inline quint8 chanG(pixel_t p) { return quint8(p >> 8); }
inline quint8 chanB(pixel_t p) { return quint8(p); }
inline quint8 getA(pixel_t p) { return quint8(p >> 24); }
/// Un-premultiplies a single channel given the pixel's alpha.
inline quint8 unpmulCh(quint8 c, quint8 a)
{
    if (a == 0)
        return 0;
    if (a == 255)
        return c;
    return quint8((int(c) * 255 + int(a) / 2) / int(a));
}
inline quint8 getR(pixel_t p) { return unpmulCh(quint8(p >> 16), quint8(p >> 24)); }
inline quint8 getG(pixel_t p) { return unpmulCh(quint8(p >> 8), quint8(p >> 24)); }
inline quint8 getB(pixel_t p) { return unpmulCh(quint8(p), quint8(p >> 24)); }

pixel_t qPremult(quint8 a, quint8 r, quint8 g, quint8 b);
inline pixel_t toPixel(const QColor& c)
{
    return qPremult(quint8(c.alpha()), quint8(c.red()), quint8(c.green()), quint8(c.blue()));
}
inline QColor toQColor(pixel_t p)
{
    return QColor(getR(p), getG(p), getB(p), getA(p));
}
inline pixel_t rgbPixel(quint8 r, quint8 g, quint8 b) { return qPremult(255, r, g, b); }
inline pixel_t whitePixel() { return 0xFFFFFFFFu; }
inline pixel_t blackPixel() { return 0xFF000000u; }
inline pixel_t transparentPixel() { return 0u; }

// ---------------------------------------------------------------- blending
/// Composites `src` over `dst` using `mode`. Both are premultiplied ARGB.
pixel_t composePixel(pixel_t dst, pixel_t src, BlendMode mode, quint8 layerOpacity = 255);
/// Raw separable blend of two non premultiplied 0-255 channels.
quint8 blendChannel(BlendMode mode, int cb, int cs, quint8 cbA = 255, quint8 csA = 255);
/// Non separable (hue/saturation/color/luminosity) blend of two non premultiplied colors.
void blendNonSeparable(BlendMode mode, int cbR, int cbG, int cbB, int csR, int csG, int csB,
                       int* outR, int* outG, int* outB);

// ---------------------------------------------------------------- conversions
QColor rgbToHsv(QColor c);
QColor hsvToRgb(QColor c);
QColor rgbToHsl(QColor c);
QColor hslToRgb(QColor c);
void rgbToHsvInt(int r, int g, int b, int* h, int* s, int* v);
void hsvToRgbInt(int h, int s, int v, int* r, int* g, int* b);

QColor shiftHue(QColor c, int shift);
QColor shiftSaturation(QColor c, int shift);
QColor shiftLightness(QColor c, int shift);
QColor shiftTemperature(QColor c, int shift);
QColor shiftTint(QColor c, int shift);
/// Perceptual distance in 0..255 range (weighted euclidean on premultiplied values).
int colorDistance(pixel_t a, pixel_t b);

QString colorToHex(pixel_t p, bool withAlpha = false);
pixel_t colorFromHex(const QString& s, bool* ok = nullptr);
QString colorToHsvString(pixel_t p);

/// Stable name for a color ("Red", "Cornflower Blue", ...) if known.
QString colorName(quint8 r, quint8 g, quint8 b);
QString colorName(pixel_t p);

/// 0xRRGGBB -> pixel (opaque)
inline pixel_t rgb(int hex)
{
    return rgbPixel(quint8((hex >> 16) & 0xFF), quint8((hex >> 8) & 0xFF), quint8(hex & 0xFF));
}

} // namespace pnq
