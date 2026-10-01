#include "core/Renderer.h"

namespace pnq {
namespace Renderer {

void compositeLayer(Layer* layer, Surface& dest)
{
    if (!layer || !layer->visible() || layer->opacity() == 0)
        return;
    const int w = dest.width(), h = dest.height();
    const int lw = layer->width(), lh = layer->height();
    const int x0 = qMax(0, 0), y0 = 0;
    Q_UNUSED(x0);
    Q_UNUSED(y0);
    const int x1 = qMin(w, lw), y1 = qMin(h, lh);
    const quint8 op = quint8(layer->opacity());
    const BlendMode mode = layer->blendMode();
    for (int y = y0; y < y1; ++y) {
        const pixel_t* ls = layer->surface().scanLine(y);
        pixel_t* ds = dest.scanLine(y);
        for (int x = 0; x < x1; ++x) {
            if (ls[x] == 0)
                continue;
            ds[x] = composePixel(ds[x], ls[x], mode, op);
        }
    }
}

void compositeRegion(const QVector<Layer*>& layers, const QRect& imageBounds, Surface& dest,
                     const QPoint& offset)
{
    if (dest.width() != imageBounds.width() || dest.height() != imageBounds.height())
        dest = Surface(imageBounds.size());
    else
        dest.clear(); // the destination always starts fully transparent
    for (Layer* l : layers) {
        if (!l->visible() || l->opacity() == 0)
            continue;
        const int w = dest.width(), h = dest.height();
        const int x0 = qMax(0, -offset.x());
        const int y0 = qMax(0, -offset.y());
        const int x1 = qMin(w, l->width() - offset.x());
        const int y1 = qMin(h, l->height() - offset.y());
        const quint8 op = quint8(l->opacity());
        const BlendMode mode = l->blendMode();
        for (int y = y0; y < y1; ++y) {
            const pixel_t* ls = l->surface().scanLine(y + offset.y());
            pixel_t* ds = dest.scanLine(y);
            if (!ls || !ds)
                continue;
            for (int x = x0; x < x1; ++x) {
                const pixel_t sp = ls[x + offset.x()];
                if (sp == 0)
                    continue;
                ds[x] = composePixel(ds[x], sp, mode, op);
            }
        }
    }
}

void composite(const QVector<Layer*>& layers, int w, int h, Surface& dest)
{
    if (dest.width() != w || dest.height() != h)
        dest = Surface(w, h);
    else
        dest.clear();
    for (Layer* l : layers)
        compositeLayer(l, dest);
}

void applyMask(Surface& surface, const Selection& selection, bool clearOutside)
{
    if (selection.isNull())
        return;
    const int w = surface.width(), h = surface.height();
    const QImage& m = selection.mask();
    const int mw = qMin(w, m.width()), mh = qMin(h, m.height());
    for (int y = 0; y < mh; ++y) {
        pixel_t* row = surface.scanLine(y);
        const uchar* mv = m.constScanLine(y);
        for (int x = 0; x < mw; ++x) {
            const int v = mv[x];
            if (v == 255)
                continue;
            if (v == 0) {
                if (clearOutside)
                    row[x] = 0;
                continue;
            }
            row[x] = qPremult(quint8(getA(row[x]) * v / 255), quint8(getR(row[x]) * v / 255),
                              quint8(getG(row[x]) * v / 255), quint8(getB(row[x]) * v / 255));
        }
    }
}

Surface croppedToSelection(const Surface& src, const Selection& sel)
{
    Surface out = src;
    applyMask(out, sel, true);
    return out;
}

Surface maskedCopy(const Surface& src, const Selection& sel)
{
    Surface out(src.width(), src.height());
    for (int y = 0; y < src.height(); ++y) {
        const pixel_t* s = src.scanLine(y);
        pixel_t* d = out.scanLine(y);
        const uchar* m = sel.isNull() ? nullptr : sel.mask().constScanLine(y);
        for (int x = 0; x < src.width(); ++x) {
            if (!m || m[x] == 255) {
                d[x] = s[x];
            } else if (m[x] != 0) {
                d[x] = qPremult(quint8(getA(s[x]) * m[x] / 255), quint8(getR(s[x]) * m[x] / 255),
                                quint8(getG(s[x]) * m[x] / 255), quint8(getB(s[x]) * m[x] / 255));
            }
        }
    }
    return out;
}

void combineSurfaces(Surface& dst, const Surface& src, const Selection& sel, BlendMode mode)
{
    const int w = dst.width(), h = dst.height();
    for (int y = 0; y < h; ++y) {
        pixel_t* d = dst.scanLine(y);
        const pixel_t* s = y < src.height() ? src.scanLine(y) : nullptr;
        const uchar* m = sel.isNull() ? nullptr : sel.mask().constScanLine(y);
        for (int x = 0; x < w; ++x) {
            if (!s)
                continue;
            pixel_t sp = s[x];
            if (m) {
                const int mv = m[x];
                if (mv == 0)
                    continue;
                if (mv != 255)
                    sp = qPremult(quint8(getA(sp) * mv / 255), quint8(getR(sp) * mv / 255),
                                  quint8(getG(sp) * mv / 255), quint8(getB(sp) * mv / 255));
            }
            if (sp == 0)
                continue;
            d[x] = composePixel(d[x], sp, mode);
        }
    }
}

} // namespace Renderer
} // namespace pnq
