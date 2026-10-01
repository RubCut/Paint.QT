#include "core/ImageOps.h"
#include "core/History.h"
#include "core/ImageMath.h"
#include "core/Renderer.h"

#include <QPainter>
#include <algorithm>
#include <QTransform>
#include <QtMath>

namespace pnq {
namespace ImageOps {

void resizeSurface(Surface& s, int w, int h, bool smooth, ResizeMode mode, pixel_t padColor)
{
    w = qMax(1, w);
    h = qMax(1, h);
    if (s.isNull())
        return;
    double ar = double(s.width()) / s.height();
    if (mode == ResizeMode::PreserveAspect) {
        if (double(w) / h > ar)
            w = int(h * ar + 0.5);
        else
            h = int(w / ar + 0.5);
    }
    if (mode == ResizeMode::Normal || mode == ResizeMode::DoNotPreserveAspect) {
        if (s.width() == w && s.height() == h)
            return;
        s = s.scaled(w, h, smooth);
        return;
    }
    QImage src = s.toQImage();
    QImage dst(w, h, QImage::Format_ARGB32_Premultiplied);
    QColor pad(toQColor(padColor));
    dst.fill(pad);
    QPainter p(&dst);
    p.setRenderHint(QPainter::SmoothPixmapTransform, smooth);
    if (mode == ResizeMode::Crop) {
        // Scale so the image covers the new size, keeping the centre.
        double sx = double(w) / src.width();
        double sy = double(h) / src.height();
        double f = qMax(sx, sy);
        QSize scaled(qMax(1, int(src.width() * f + 0.5)), qMax(1, int(src.height() * f + 0.5)));
        QImage sc = smooth ? src.scaled(scaled, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                           : src.scaled(scaled, Qt::IgnoreAspectRatio, Qt::FastTransformation);
        int ox = (w - sc.width()) / 2;
        int oy = (h - sc.height()) / 2;
        p.drawImage(QRect(0, 0, w, h), sc, QRect(ox, oy, w, h));
    } else if (mode == ResizeMode::Pad) {
        p.drawImage(0, 0, src);
    } else if (mode == ResizeMode::PadCenter) {
        p.drawImage((w - src.width()) / 2, (h - src.height()) / 2, src);
    } else if (mode == ResizeMode::PadTopLeft) {
        p.drawImage(QRect(0, 0, w, h), src, QRect(0, 0, w, h));
    }
    p.end();
    s = Surface::fromQImage(dst);
}

// The axis flips and quarter turns are exact: a resampling transform would
// lose up to 1 LSB per channel, which is visible on flat colour areas.
void flipHorizontal(Surface& s)
{
    if (s.isNull())
        return;
    const int w = s.width(), h = s.height();
    for (int y = 0; y < h; ++y) {
        pixel_t* row = s.scanLine(y);
        for (int x = 0; x < w / 2; ++x)
            std::swap(row[x], row[w - 1 - x]);
    }
}

void flipVertical(Surface& s)
{
    if (s.isNull())
        return;
    const int w = s.width(), h = s.height();
    for (int y = 0; y < h / 2; ++y) {
        pixel_t* a = s.scanLine(y);
        pixel_t* b = s.scanLine(h - 1 - y);
        for (int x = 0; x < w; ++x)
            std::swap(a[x], b[x]);
    }
}

void rotate180(Surface& s)
{
    flipHorizontal(s);
    flipVertical(s);
}

void rotate90(Surface& s, bool counterClockwise)
{
    if (s.isNull())
        return;
    const int w = s.width(), h = s.height();
    Surface out(h, w);
    for (int y = 0; y < h; ++y) {
        const pixel_t* src = s.scanLine(y);
        for (int x = 0; x < w; ++x) {
            // 90 deg CCW: (x, y) -> (y, w - 1 - x)
            const int dx = counterClockwise ? y : h - 1 - y;
            const int dy = counterClockwise ? w - 1 - x : x;
            out.setPixel(dx, dy, src[x]);
        }
    }
    s = out;
}

void rotate(Surface& s, double degrees, bool maintainSize, bool smooth)
{
    if (s.isNull())
        return;
    const double rad = degrees * M_PI / 180.0;
    const double c = std::cos(rad), sn = std::sin(rad);
    const int w = s.width(), h = s.height();
    if (maintainSize) {
        QTransform t;
        t.translate(w / 2.0, h / 2.0);
        t.rotate(degrees);
        t.translate(-w / 2.0, -h / 2.0);
        QImage img = s.toQImage();
        QImage dst(w, h, QImage::Format_ARGB32_Premultiplied);
        dst.fill(Qt::transparent);
        {
            QPainter p(&dst);
            p.setRenderHint(QPainter::SmoothPixmapTransform, smooth);
            p.setRenderHint(QPainter::Antialiasing, smooth);
            p.translate(w / 2.0, h / 2.0);
            p.rotate(degrees);
            p.drawImage(QRectF(-w / 2.0, -h / 2.0, w, h), img, QRectF(0, 0, w, h));
        }
        s = Surface::fromQImage(dst);
        return;
    }
    const double newW = std::abs(w * c) + std::abs(h * sn);
    const double newH = std::abs(w * sn) + std::abs(h * c);
    QImage img = s.toQImage();
    QImage dst(qRound(newW), qRound(newH), QImage::Format_ARGB32_Premultiplied);
    dst.fill(Qt::transparent);
    {
        QPainter p(&dst);
        p.setRenderHint(QPainter::SmoothPixmapTransform, smooth);
        p.translate(dst.width() / 2.0, dst.height() / 2.0);
        p.rotate(degrees);
        p.drawImage(QRectF(-w / 2.0, -h / 2.0, w, h), img, QRectF(0, 0, w, h));
    }
    s = Surface::fromQImage(dst);
}

void scaleByPercent(Surface& s, double percent, bool smooth)
{
    const int w = qMax(1, int(s.width() * percent / 100.0 + 0.5));
    const int h = qMax(1, int(s.height() * percent / 100.0 + 0.5));
    s = s.scaled(w, h, smooth);
}

// ------------------------------------------------------------------ document

static void transformLayers(Document& doc, const QString& name,
                            const std::function<void(Surface&)>& fn)
{
    doc.history()->beginMacro(name);
    for (int i = 0; i < doc.layerCount(); ++i) {
        Layer* l = doc.layerAt(i);
        Surface before = l->surface().copy();
        Surface after = before.copy();
        fn(after);
        l->setSurface(after);
        l->markThumbnailDirty();
        doc.history()->push(new SurfaceAction(name, i, before, after));
        doc.notifyLayerPixels(i, l->bounds());
    }
    doc.history()->endMacro();
}

void flipDocument(Document& doc, bool horizontal)
{
    transformLayers(doc, QObject::tr(horizontal ? "Flip Horizontal" : "Flip Vertical"),
                    [&](Surface& s) {
                        if (horizontal)
                            flipHorizontal(s);
                        else
                            flipVertical(s);
                    });
    if (!doc.selection().isNull()) {
        Selection s = doc.selection();
        // Mirror the mask.
        QImage m = s.mask();
        QImage out(m.size(), QImage::Format_Alpha8);
        for (int y = 0; y < m.height(); ++y) {
            const uchar* src = m.constScanLine(y);
            uchar* dst = out.scanLine(y);
            for (int x = 0; x < m.width(); ++x)
                dst[x] = src[horizontal ? (m.width() - 1 - x) : x];
        }
        s.setMask(out);
        doc.setSelection(s);
    }
}

void rotateDocument(Document& doc, double degrees, bool maintainSize)
{
    transformLayers(doc, QObject::tr("Rotate Image"), [&](Surface& s) { rotate(s, degrees, maintainSize, true); });
}

void rotateLayers(Document& doc, double degrees, bool maintainSize)
{
    transformLayers(doc, QObject::tr("Rotate Layer"), [&](Surface& s) { rotate(s, degrees, maintainSize, true); });
}

void flipLayers(Document& doc, bool horizontal)
{
    transformLayers(doc, horizontal ? QObject::tr("Flip Layer Horizontal") : QObject::tr("Flip Layer Vertical"),
                    [&](Surface& s) {
                        if (horizontal)
                            flipHorizontal(s);
                        else
                            flipVertical(s);
                    });
}

void resizeDocument(Document& doc, int w, int h, bool smooth, ResizeMode mode, bool allLayers,
                    pixel_t padColor)
{
    const QString name = (mode == ResizeMode::Crop) ? QObject::tr("Crop Image") : QObject::tr("Resize Image");
    doc.history()->beginMacro(name);
    for (int i = 0; i < doc.layerCount(); ++i) {
        Layer* l = doc.layerAt(i);
        Surface before = l->surface().copy();
        Surface after = before.copy();
        resizeSurface(after, w, h, smooth, mode, padColor);
        l->setSurface(after);
        l->markThumbnailDirty();
        doc.history()->push(new SurfaceAction(name, i, before, after));
        doc.notifyLayerPixels(i, l->bounds());
    }
    doc.history()->endMacro();

    // The canvas follows for plain "resize image".
    if (mode == ResizeMode::Normal || mode == ResizeMode::PreserveAspect
        || mode == ResizeMode::DoNotPreserveAspect) {
        int nw = qMax(1, w), nh = qMax(1, h);
        if (mode == ResizeMode::PreserveAspect) {
            double ar = double(doc.width()) / doc.height();
            if (double(nw) / nh > ar)
                nw = int(nh * ar + 0.5);
            else
                nh = int(nw / ar + 0.5);
        }
        Selection selBefore = doc.selection();
        Selection ns = selBefore;
        if (!selBefore.isNull()) {
            QImage m2(nw, nh, QImage::Format_Alpha8);
            m2.fill(0);
            QPainter p(&m2);
            p.drawImage(QPoint(0, 0), selBefore.mask());
            ns.setMask(m2);
        }
        doc.history()->push(new CanvasBoundsAction(name, doc.width(), doc.height(), selBefore, nw, nh,
                                                   ns));
        doc.setCanvasBoundsOnly(nw, nh, ns);
    }
}

void clearSelection(Document& doc)
{
    Layer* l = doc.activeLayer();
    if (!l)
        return;
    QRect r = doc.selection().isNull() ? doc.bounds() : doc.selection().nonEmptyRect().intersected(doc.bounds());
    if (r.isEmpty())
        return;
    Surface before = l->surface().cropped(r);
    l->surface().blendRect(r, 0);
    l->markThumbnailDirty();
    Surface after(r.size());
    doc.history()->push(
        new PixelDeltaAction(doc.tr("Clear"), doc.activeLayerIndex(), r, before, after));
    doc.notifyLayerPixels(doc.activeLayerIndex(), r);
}

void copySelection(Document& doc, Surface* outPixels, bool merged)
{
    const QRect r = doc.selectionBounds();
    if (outPixels) {
        if (merged) {
            *outPixels = doc.compositeSurface().cropped(r);
        } else {
            Layer* l = doc.activeLayer();
            if (!l)
                return;
            *outPixels = Renderer::maskedCopy(l->surface().cropped(r), doc.selection());
        }
    }
}

void cutSelection(Document& doc, Surface* outPixels)
{
    copySelection(doc, outPixels, false);
    clearSelection(doc);
}

Layer* pasteAsNewLayer(Document& doc, const Surface& pixels, int atX, int atY, const QString& name)
{
    Layer* l = doc.addLayer(name.isEmpty() ? doc.tr("Pasted Layer") : name);
    Surface s(doc.width(), doc.height());
    s.blendFrom(pixels, QPoint(atX, atY), pixels.bounds());
    l->setSurface(s);
    l->markThumbnailDirty();
    doc.history()->push(new LayerStructureAction(doc.tr("Paste"), LayerStructureAction::Kind::Add,
                                                 doc.activeLayerIndex(), nullptr, l));
    doc.notifyLayerStructure();
    return l;
}

void pasteIntoLayer(Document& doc, const Surface& pixels, int atX, int atY, int targetLayerIndex)
{
    Layer* l = doc.layerAt(targetLayerIndex);
    if (!l)
        return;
    // Clip against the layer's own surface, not just the document: a layer can be
    // smaller than the document, and clipping against the document alone would
    // drop the pixels while still recording an undo step that changes nothing.
    const QRect r = QRect(atX, atY, pixels.width(), pixels.height())
                       .intersected(doc.bounds())
                       .intersected(l->surface().bounds());
    if (r.isEmpty())
        return;
    Surface before = l->surface().cropped(r);
    // Blend rather than overwrite: the transparent margins around a copied
    // selection must not punch holes in the layer underneath.
    l->surface().blendFrom(pixels, QPoint(atX, atY), pixels.bounds());
    l->markThumbnailDirty();
    Surface after = l->surface().cropped(r);
    doc.history()->push(new PixelDeltaAction(doc.tr("Paste"), targetLayerIndex, r, before, after));
    doc.notifyLayerPixels(targetLayerIndex, r);
}

Surface compositeSelected(const Document& doc)
{
    return Renderer::maskedCopy(doc.compositeSurface(), doc.selection());
}

} // namespace ImageOps
} // namespace pnq
