#pragma once

#include "core/ColorUtils.h"
#include "core/Selection.h"
#include "core/Surface.h"

#include <QImage>
#include <QString>
#include <QStringList>
#include <functional>

namespace pnq {

class Document;
namespace Effects {

// ============================================================== helpers
/// Per-pixel callback applied inside the selection.
void forEachPixel(const Surface& src, const Selection& sel,
                  const std::function<void(int x, int y, pixel_t& p)>& fn);

QString colorAdjustmentsCategory();
QString colorEffectsCategory();
QString blurCategory();
QString stylizeCategory();
QString noiseCategory();
QString layersCategory();
QString miscCategory();

// ============================================================== Adjustments
void brightnessContrast(Surface& s, const Selection& sel, int brightness, int contrast);
void brightness(Surface& s, const Selection& sel, int value);
void contrast(Surface& s, const Selection& sel, int value);
void curves(Surface& s, const Selection& sel, const QVector<int>& rgbPoints, const QVector<int>& redPoints,
            const QVector<int>& greenPoints, const QVector<int>& bluePoints);
void levels(Surface& s, const Selection& sel, int inputLow, int inputHigh, int outputLow, int outputHigh,
            double gamma, bool useRgb, int redLow, int redHigh, double redGamma, int greenLow,
            int greenHigh, double greenGamma, int blueLow, int blueHigh, double blueGamma);
void hueSaturation(Surface& s, const Selection& sel, int hue, int saturation, int lightness, bool colorize);
void colorBalance(Surface& s, const Selection& sel, int shadowsCyanRed, int shadowsMagentaGreen,
                  int shadowsYellowBlue, int midtonesCyanRed, int midtonesMagentaGreen,
                  int midtonesYellowBlue, int highlightsCyanRed, int highlightsMagentaGreen,
                  int highlightsYellowBlue, bool preserveLuminosity);
void desaturate(Surface& s, const Selection& sel, int amount, bool hslWeights, int low, int mid, int high,
                bool switchColors = false);
void invert(Surface& s, const Selection& sel);
void posterize(Surface& s, const Selection& sel, int levels, int dithering);
void threshold(Surface& s, const Selection& sel, int amount);
void selectiveColor(Surface& s, const Selection& sel, int reds, int yellows, int greens, int cyans,
                    int blues, int magentas, int neutrals, bool relative, int cmykC, int cmykM,
                    int cmykY, int cmykK);
void temperatureTint(Surface& s, const Selection& sel, int temperature, int tint);
void replaceColor(Surface& s, const Selection& sel, pixel_t from, pixel_t to, int tolerance,
                  bool useHue, int hueShift, int saturation, int lightness);
void channelMixer(Surface& s, const Selection& sel, int rr, int rg, int rb, int gr, int gg, int gb, int br,
                  int bg, int bb, bool monochrome, int mr, int mg, int mb);
void autoAdjustLevels(Surface& s, const Selection& sel);

// ============================================================== Effects - Blur
void blurGaussian(Surface& s, const Selection& sel, double radius, bool monochrome, bool deepAnalysis);
void blurBox(Surface& s, const Selection& sel, double radius, bool monochrome);
void blurMotion(Surface& s, const Selection& sel, double angle, double sampleCount);
void blurZoom(Surface& s, const Selection& sel, int cx, int cy, int amount);
void blurRadial(Surface& s, const Selection& sel, int cx, int cy, int amount);
void blurSurface(Surface& s, const Selection& sel, double strength, double colorStrength, double size,
                 bool monochrome, int seed);
void glow(Surface& s, const Selection& sel, int radius, int intensity, pixel_t glowColor, bool centerAura);
void shadow(Surface& s, const Selection& sel, int blurRadius, int offsetX, int offsetY, pixel_t shadowColor,
            int opacity);
void dropShadow(Surface& s, const Selection& sel, int blurRadius, int offsetX, int offsetY,
                pixel_t shadowColor, int opacity);
void innerShadow(Surface& s, const Selection& sel, int blurRadius, int offsetX, int offsetY,
                 pixel_t shadowColor, int opacity, bool invertSelection);

// ============================================================== Effects - Stylize
void emboss(Surface& s, const Selection& sel, double depth, double azimuth, double elevation,
            bool monochrome);
void invertEmboss(Surface& s, const Selection& sel, double depth, double azimuth, double elevation);
void edgeDetect(Surface& s, const Selection& sel, double depth, double azimuth, double elevation);
void sobelEdges(Surface& s, const Selection& sel);
void sharpen(Surface& s, const Selection& sel, double amount, double radius, bool monochrome);
void oldFilm(Surface& s, const Selection& sel, int intensity, int monochrome, int matrix, int noise,
             double vignette, double tint);
void vignette(Surface& s, const Selection& sel, int cx, int cy, double start, double end, double feather,
              bool invert, bool centerColor, double brightness, double saturation);
void pixelate(Surface& s, const Selection& sel, int blockWidth, int blockHeight, bool normal);
void mosaic(Surface& s, const Selection& sel, int cellSize, int sample);
void celShading(Surface& s, const Selection& sel, int levels, double threshold, double smoothing);
void oilPaint(Surface& s, const Selection& sel, int radius, int levels);
void posterizeEdges(Surface& s, const Selection& sel, int posterizeLevels, int edgePosterizeLevels,
                    int edgeThreshold, int edgeThickness);
void diffuseGlow(Surface& s, const Selection& sel, double amount, double chroma, int iterations);
void glowWarped(Surface& s, const Selection& sel, int radius, int intensity, int warp, int radial,
                pixel_t color, bool centerAura);
void crystalize(Surface& s, const Selection& sel, int cellSize);
void ripple(Surface& s, const Selection& sel, double amplitude, double frequency, double phase, int direction);
void waterColor(Surface& s, const Selection& sel, int distortion);
void sunburst(Surface& s, const Selection& sel, int cx, int cy, int rays, double brightness, bool invert);
void recursiveDescent(Surface& s, const Selection& sel, double strength, double monotone);
void unsharpMask(Surface& s, const Selection& sel, double amount, double radius, int threshold);

// ============================================================== Effects - Noise
void addNoise(Surface& s, const Selection& sel, int amount, bool uniform, bool monochrome, bool correlated,
              quint32 seed);
void clouds(Surface& s, const Selection& sel, double size, double seed, pixel_t baseColor);
void fractalNoise(Surface& s, const Selection& sel, double octaveCount, double persistence, int seed,
                  bool turbulence, int channel);
void turbulenceNoise(Surface& s, const Selection& sel, int seed);
void medianFilter(Surface& s, const Selection& sel, int radius);
void surfaceNoise(Surface& s, const Selection& sel, int size, double strength, bool monochrome,
                  quint32 seed);
void stretchDents(Surface& s, const Selection& sel, int size, int length, double amplitude,
                  const QPoint& center);

// ============================================================== Composite ops
void rotateFlip(Surface& s, double degrees, bool maintainSize, bool smooth);
void pixelateAlpha(const QImage& src, int blockWidth, int blockHeight);

} // namespace Effects
} // namespace pnq
