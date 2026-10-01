#pragma once

#include "core/BlendMode.h"
#include "core/ColorUtils.h"
#include "core/Layer.h"
#include "core/Selection.h"
#include "core/Surface.h"

namespace pnq {

namespace Renderer {

/// Composites all layers bottom-to-top into `dest` (which is resized/cleared).
void composite(const QVector<Layer*>& layers, int w, int h, Surface& dest);

/// Composites the sub-rectangle starting at `offset` of the image.
void compositeRegion(const QVector<Layer*>& layers, const QRect& imageBounds, Surface& dest,
                     const QPoint& offset);

/// Composites a single layer onto dest.
void compositeLayer(Layer* layer, Surface& dest);

/// Zeros out everything outside the selection.
void applyMask(Surface& surface, const Selection& selection, bool clearOutside = true);

/// Returns a copy of the layer restricted to the selection (transparent outside).
Surface croppedToSelection(const Surface& src, const Selection& sel);

/// Intersects a source surface with a selection producing a new surface.
Surface maskedCopy(const Surface& src, const Selection& sel);

/// Merges two surfaces (b over a) inside the selection.
void combineSurfaces(Surface& dst, const Surface& src, const Selection& sel, BlendMode mode);

} // namespace Renderer
} // namespace pnq
