#pragma once

#include "core/Document.h"
#include "core/Selection.h"
#include "core/Surface.h"

namespace pnq {

namespace ImageOps {

enum class ResizeMode {
    Normal,
    PreserveAspect,
    DoNotPreserveAspect,
    Crop,
    Pad,
    PadCenter,
    PadTopLeft
};

void resizeSurface(Surface& s, int w, int h, bool smooth, ResizeMode mode, pixel_t padColor = 0xFF000000u);

void flipHorizontal(Surface& s);
void flipVertical(Surface& s);
void rotate90(Surface& s, bool counterClockwise);
void rotate180(Surface& s);
/// Rotates by an arbitrary angle; when `maintainSize` the canvas is kept.
void rotate(Surface& s, double degrees, bool maintainSize, bool smooth);

void scaleByPercent(Surface& s, double percent, bool smooth);

// --- document level (with undo) ---
void flipDocument(Document& doc, bool horizontal);
void rotateDocument(Document& doc, double degrees, bool maintainSize);
void resizeDocument(Document& doc, int w, int h, bool smooth, ResizeMode mode, bool allLayers,
                    pixel_t padColor = 0xFF000000u);
void rotateLayers(Document& doc, double degrees, bool maintainSize);
void flipLayers(Document& doc, bool horizontal);

/// Merges the selection content down one layer, clearing the source.
void cutSelection(Document& doc, Surface* outPixels = nullptr);
void copySelection(Document& doc, Surface* outPixels = nullptr, bool merged = false);
void pasteIntoLayer(Document& doc, const Surface& pixels, int atX, int atY, int targetLayerIndex);
Layer* pasteAsNewLayer(Document& doc, const Surface& pixels, int atX, int atY, const QString& name);
void clearSelection(Document& doc);
void selectAllAndDelete(Document& doc);

/// Turns the selection into a layer boundary (not in PDN, but handy).
Surface compositeSelected(const Document& doc);

} // namespace ImageOps
} // namespace pnq
