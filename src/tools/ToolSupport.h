#pragma once

#include "core/ColorUtils.h"
#include "core/History.h"
#include "core/Selection.h"
#include "core/Surface.h"

#include <QHash>
#include <QRect>
class QPainter;

namespace pnq {

/// Captures the "before" state of a surface lazily, in tiles, so long strokes
/// do not need a full copy of the layer. Used by every painting tool.
class StrokeBuffer
{
public:
    void begin(Surface* target);
    /// Marks a region as about to be modified (captures the needed tiles).
    void touch(const QRect& r);
    /// Ends the session and returns the union of all touched regions.
    QRect end() { return m_dirty; }
    QRect dirty() const { return m_dirty; }
    bool isEmpty() const { return m_dirty.isNull(); }

    /// The captured tiles, clipped to the surface. Only these hold real "before"
    /// data: the bounding box returned by end() also covers the corners a
    /// diagonal stroke never crossed, and those corners have no saved pixels.
    QVector<QRect> tileRects() const;
    /// The original pixels of one tile rectangle, as stored by touch().
    Surface beforeIn(const QRect& region) const;
    /// The current pixels of the same rectangle.
    Surface afterIn(const QRect& region) const;

    Surface before() const;
    Surface after() const;

private:
    static constexpr int TileSize = 64;
    struct Key {
        int x, y;
        bool operator==(const Key& o) const { return x == o.x && y == o.y; }
    };
    friend uint qHash(const Key& k, uint s)
    {
        return ::qHash(k.x * 73856093 ^ k.y * 19349663, s);
    }

    void captureTile(int tx, int ty);
    /// Shared by before()/after(): gathers one rectangle out of the tiles.
    Surface gather(const QRect& region, bool wantBefore) const;

    Surface* m_target = nullptr;
    QRect m_dirty;
    QHash<Key, QImage> m_tiles;
};

/// Convenience: draws the selection marching-ants outline.
void drawSelectionOutline(QPainter& p, const Selection& sel, const QColor& color, const QColor& bg,
                          double phase);

class Document;
class Tool;

/// Commits a freehand stroke as exactly one undo step.
///
/// The stroke's saved pixels live in the tiles the brush actually crossed, and a
/// single bounding rectangle is not usable: for a diagonal stroke it also covers
/// corners that were never touched, so restoring it would wipe them to
/// transparent. So one delta is recorded per tile and the whole set is wrapped
/// in a macro, which keeps one Ctrl+Z per stroke.
void pushStrokeAction(Tool* tool, const QString& name, int layerIndex, const StrokeBuffer& buf);

/// Returns the effective colour for tools that support "shift = temporary
/// swap of primary/secondary".
inline pixel_t shiftKeyColor(pixel_t primary, pixel_t secondary, Qt::KeyboardModifiers mods)
{
    return (mods & Qt::ShiftModifier) ? secondary : primary;
}

} // namespace pnq
