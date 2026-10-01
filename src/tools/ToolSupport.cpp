#include "tools/ToolSupport.h"

#include "core/Document.h"
#include "core/History.h"
#include "tools/Tool.h"

#include <QPen>
#include <QPainter>
#include <QPainterPath>

namespace pnq {

void pushStrokeAction(Tool* tool, const QString& name, int layerIndex, const StrokeBuffer& buf)
{
    if (!tool || !tool->document() || buf.isEmpty())
        return;
    Document* doc = tool->document();
    if (!doc->history())
        return;
    QVector<HistoryAction*> steps;
    for (const QRect& tile : buf.tileRects()) {
        const Surface before = buf.beforeIn(tile);
        const Surface after = buf.afterIn(tile);
        // A captured tile the brush ended up not painting on would only add a
        // delta that changes nothing.
        if (before.isNull() || after.isNull() || before.toQImageConst() == after.toQImageConst())
            continue;
        steps.append(new PixelDeltaAction(name, layerIndex, tile, before, after));
    }
    if (steps.isEmpty())
        return;
    if (steps.size() == 1) {
        doc->history()->push(steps.takeFirst());
        return;
    }
    MacroAction* macro = new MacroAction(name);
    for (HistoryAction* a : std::as_const(steps))
        macro->add(a);
    doc->history()->push(macro);
}

void StrokeBuffer::begin(Surface* target)
{
    m_target = target;
    m_dirty = QRect();
    m_tiles.clear();
}

void StrokeBuffer::captureTile(int tx, int ty)
{
    Key k{ tx, ty };
    if (m_tiles.contains(k))
        return;
    QImage tile(TileSize, TileSize, QImage::Format_ARGB32_Premultiplied);
    tile.fill(Qt::transparent);
    if (m_target) {
        const QRect src(tx * TileSize, ty * TileSize, TileSize, TileSize);
        if (!src.isEmpty()) {
            for (int y = 0; y < TileSize; ++y) {
                const int sy = src.top() + y;
                if (sy < 0 || sy >= m_target->height())
                    continue;
                const pixel_t* srow = m_target->scanLine(sy);
                pixel_t* drow = reinterpret_cast<pixel_t*>(tile.scanLine(y));
                for (int x = 0; x < TileSize; ++x) {
                    const int sx = src.left() + x;
                    if (sx < 0 || sx >= m_target->width())
                        continue;
                    drow[x] = srow[sx];
                }
            }
        }
    }
    m_tiles.insert(k, tile);
}

void StrokeBuffer::touch(const QRect& r)
{
    if (r.isEmpty() || !m_target)
        return;
    QRect clipped = r.intersected(m_target->bounds());
    if (clipped.isEmpty())
        return;
    m_dirty = m_dirty.isNull() ? clipped : m_dirty.united(clipped);
    const int t0x = int(std::floor(clipped.left() / double(TileSize)));
    const int t1x = int(std::floor(clipped.right() / double(TileSize)));
    const int t0y = int(std::floor(clipped.top() / double(TileSize)));
    const int t1y = int(std::floor(clipped.bottom() / double(TileSize)));
    for (int ty = t0y; ty <= t1y; ++ty)
        for (int tx = t0x; tx <= t1x; ++tx)
            captureTile(tx, ty);
}

QVector<QRect> StrokeBuffer::tileRects() const
{
    QVector<QRect> out;
    if (m_dirty.isNull() || !m_target)
        return out;
    for (auto it = m_tiles.constBegin(); it != m_tiles.constEnd(); ++it) {
        const QRect tile(it.key().x * TileSize, it.key().y * TileSize, TileSize, TileSize);
        const QRect clipped = tile.intersected(m_target->bounds());
        if (!clipped.isEmpty())
            out.append(clipped);
    }
    return out;
}

Surface StrokeBuffer::gather(const QRect& region, bool wantBefore) const
{
    if (region.isEmpty())
        return Surface();
    if (!wantBefore)
        return m_target ? m_target->cropped(region) : Surface();
    Surface out(region.size());
    for (auto it = m_tiles.constBegin(); it != m_tiles.constEnd(); ++it) {
        const QRect tile(it.key().x * TileSize, it.key().y * TileSize, TileSize, TileSize);
        if (!tile.intersects(region))
            continue;
        const int dx = tile.left() - region.left();
        const int dy = tile.top() - region.top();
        for (int y = 0; y < TileSize; ++y) {
            const int ty = dy + y;
            if (ty < 0 || ty >= region.height())
                continue;
            const pixel_t* s = reinterpret_cast<const pixel_t*>(it.value().constScanLine(y));
            pixel_t* d = out.scanLine(ty);
            for (int x = 0; x < TileSize; ++x) {
                const int tx = dx + x;
                if (tx < 0 || tx >= region.width())
                    continue;
                d[tx] = s[x];
            }
        }
    }
    return out;
}

Surface StrokeBuffer::beforeIn(const QRect& region) const
{
    return gather(region, true);
}

Surface StrokeBuffer::afterIn(const QRect& region) const
{
    return gather(region, false);
}

Surface StrokeBuffer::before() const
{
    if (m_dirty.isNull())
        return Surface();
    return gather(m_dirty, true);
}

Surface StrokeBuffer::after() const
{
    if (m_dirty.isNull() || !m_target)
        return Surface();
    return m_target->cropped(m_dirty);
}

void drawSelectionOutline(QPainter& p, const Selection& sel, const QColor& color, const QColor& bg,
                          double phase)
{
    if (sel.isNull())
        return;
    const QImage& mask = sel.mask();
    const int w = mask.width(), h = mask.height();
    // Trace the boundary: horizontal runs where the mask changes value.
    QVector<QPointF> pts;
    for (int y = 0; y <= h; ++y) {
        const uchar* row = (y < h) ? mask.constScanLine(y) : nullptr;
        const uchar* rowAbove = (y > 0 && y <= h) ? mask.constScanLine(y - 1) : nullptr;
        for (int x = 0; x <= w; ++x) {
            const bool on = row && x < w && row[x] > 127;
            const bool onAbove = rowAbove && x < w && rowAbove[x] > 127;
            if (on != onAbove) {
                pts.append(QPointF(x, y));
            }
        }
    }
    Q_UNUSED(phase);
    if (pts.isEmpty())
        return;
    p.save();
    p.setPen(QPen(color, 1.0));
    for (const QPointF& pt : pts)
        p.drawPoint(pt);
    p.setPen(QPen(bg, 1.0, Qt::DotLine));
    for (const QPointF& pt : pts)
        p.drawPoint(pt + QPointF(1, 1));
    p.restore();
}

} // namespace pnq
