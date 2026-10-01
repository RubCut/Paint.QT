#pragma once

#include "core/Surface.h"

#include <QImage>
#include <QRect>
#include <QVector>

namespace pnq {

class Selection;

namespace ImageMath {

/// Separable convolution on a premultiplied surface. Edges are clamped.
void convolve(Surface& surf, const QVector<float>& kernel);
void convolveAlpha(QImage& alpha8, const QVector<float>& kernel);

QVector<float> gaussianKernel(float radius);
QVector<float> boxKernel(int radius);
QVector<float> triangleKernel(float radius);

void gaussianBlurAlpha(QImage& alpha8, float radius);

void dilate(QImage& alpha8, int radius, QImage* out = nullptr);
void erode(QImage& alpha8, int radius, QImage* out = nullptr);

/// Sharpens a premultiplied surface with an unsharp mask.
void unsharpMask(Surface& surf, float radius, int amount, int threshold);

/// Box blur used by the blur tool.
void boxBlur(Surface& surf, int radius);

/// Scales a selection mask value.
void scaleAlpha(QImage& alpha8, float factor);
/// Multiplies the selection's effect on pixels while painting.
void modulate(const Selection& sel, Surface& surf, float strength);

/// Fills the surface area (in `mask`) with `color` using scanline flood fill.
void floodFill(Surface& surf, const QPoint& seed, pixel_t color, int tolerance, int fillSelection,
               QImage* maskOut);

/// Nearest neighbour sampling of a mask with smooth (bilinear) interpolation.
int sampleMask(const QImage& mask, qreal x, qreal y, bool smooth = true);

/// Clamps a rectangle inside the image.
QRect clampRect(const QRect& r, const QSize& size);
/// Returns a rect covering a line (Bresenham bounds).
QRect lineRect(const QPoint& p0, const QPoint& p1);

/// Smooth (antialiased) line rasterisation into a destination with per-pixel alpha.
void drawSmoothLine(Surface& dst, const QPointF& p0, const QPointF& p1, pixel_t color,
                    const Selection* selection, double opacity, int width = 1);

/// Antialiased polygon fill via QPainter path rendering into a surface.
void fillPath(Surface& dst, const class QPainterPath& path, pixel_t color, const Selection* selection,
              double opacity);

/// Draws a path outline.
void strokePath(Surface& dst, const class QPainterPath& path, pixel_t color, const Selection* selection,
                double opacity, double strokeWidth, bool antialias = true);

} // namespace ImageMath
} // namespace pnq
