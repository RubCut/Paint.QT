#include "core/Document.h"
#include "core/History.h"
#include "core/ImageMath.h"
#include "core/Renderer.h"

#include <QPainter>
#include <algorithm>

namespace pnq {

Document::Document(int width, int height, QObject* parent)
    : QObject(parent)
    , m_width(qMax(1, width))
    , m_height(qMax(1, height))
{
    m_history = new History(this, this);
    m_history->setMaxLength(m_maxHistory);
}

Document::~Document()
{
    qDeleteAll(m_layers);
    m_layers.clear();
}

Layer* Document::layerAt(int index) const
{
    if (index < 0 || index >= m_layers.size())
        return nullptr;
    return m_layers[index];
}

void Document::setActiveLayerIndex(int index)
{
    if (index < 0 || index >= m_layers.size() || index == m_activeLayer)
        return;
    m_activeLayer = index;
    emit activeLayerChanged(m_activeLayer);
}

QRect Document::clippedToCanvas(const QRect& r) const
{
    return r.intersected(bounds());
}

void Document::setDirty(bool d)
{
    if (m_dirty == d)
        return;
    m_dirty = d;
    emit dirtyChanged(m_dirty);
}

void Document::markSaved()
{
    setDirty(false);
}

// ------------------------------------------------------------------ history helpers

void Document::notifyLayerPixels(int index, const QRect& r)
{
    emit layerPixelsChanged(index, r);
    setDirty(true);
}

void Document::notifyLayerStructure()
{
    emit layersChanged();
    emit layerStructureChanged();
    setDirty(true);
}

void Document::setMaxHistoryLength(int n)
{
    m_maxHistory = n;
    m_history->setMaxLength(n);
}

int Document::maxHistoryLength() const
{
    return m_maxHistory;
}

QString Document::displayName() const
{
    if (m_filePath.isEmpty())
        return tr("Untitled");
    QFileInfo fi(m_filePath);
    return fi.fileName();
}

// ------------------------------------------------------------------ composite

QImage Document::compositeImage() const
{
    return compositeSurface().toQImage();
}

Surface Document::compositeSurface() const
{
    Surface out;
    Renderer::composite(m_layers, m_width, m_height, out);
    return out;
}

QImage Document::layerThumbnail(int index, int maxSize) const
{
    Layer* l = layerAt(index);
    if (!l)
        return QImage();
    if (l->thumbnailDirty()) {
        l->setThumbnailDirty(false);
    }
    return l->thumbnail(maxSize);
}

// ------------------------------------------------------------------ layers

Layer* Document::addLayer(const QString& name, int index)
{
    Layer* l = new Layer(m_width, m_height);
    if (!name.isEmpty())
        l->setName(name);
    insertLayer(l, index);
    return l;
}

Layer* Document::insertLayer(Layer* layer, int index)
{
    if (!layer)
        return nullptr;
    int at = (index < 0 || index > m_layers.size()) ? m_layers.size() : index;
    m_layers.insert(at, layer);
    m_activeLayer = at;
    notifyLayerStructure();
    emit activeLayerChanged(m_activeLayer);
    return layer;
}

void Document::removeLayerAt(int index)
{
    Layer* l = layerAt(index);
    if (!l || m_layers.size() <= 1)
        return;
    m_layers.removeAt(index);
    delete l;
    m_activeLayer = qBound(0, qMin(index, m_layers.size() - 1), m_layers.size() - 1);
    notifyLayerStructure();
    emit activeLayerChanged(m_activeLayer);
}

Layer* Document::duplicateLayerAt(int index)
{
    Layer* l = layerAt(index);
    if (!l)
        return nullptr;
    Layer* c = l->clone();
    c->setName(tr("%1 copy").arg(l->name()));
    return insertLayer(c, index + 1);
}

bool Document::moveLayer(int from, int to)
{
    if (from < 0 || from >= m_layers.size() || to < 0 || to >= m_layers.size() || from == to)
        return false;
    Layer* l = m_layers.takeAt(from);
    m_layers.insert(to, l);
    m_activeLayer = to;
    notifyLayerStructure();
    emit activeLayerChanged(m_activeLayer);
    return true;
}

bool Document::beginMoveLayer(int from, int to)
{
    if (from < 0 || from >= m_layers.size() || to < 0 || to >= m_layers.size() || from == to)
        return false;
    moveLayer(from, to);
    return true;
}

void Document::mergeDown(int index)
{
    if (index <= 0 || index >= m_layers.size())
        return;
    Layer* top = m_layers[index];
    Layer* bottom = m_layers[index - 1];
    if (top->lock() == LayerLock::All)
        return;
    Surface merged = bottom->surface().copy();
    Renderer::compositeLayer(top, merged);
    // The merged result is clipped to the selection, if any.
    Renderer::applyMask(merged, m_selection);
    // Clear the bottom layer outside the selection.
    Surface masked = Renderer::maskedCopy(bottom->surface().copy(), m_selection);
    bottom->setSurface(masked);
    bottom->setOpacity(255);
    bottom->setBlendMode(BlendMode::Normal);
    m_layers.removeAt(index);
    delete top;
    m_activeLayer = index - 1;
    // Adjust the selection to the merged area: after merging, the selection is
    // moved down one layer conceptually, so we keep it as-is.
    notifyLayerStructure();
    emit activeLayerChanged(m_activeLayer);
}

void Document::mergeVisible()
{
    if (m_layers.size() < 2)
        return;
    Surface merged(m_width, m_height);
    for (Layer* l : m_layers)
        Renderer::compositeLayer(l, merged);
    Renderer::applyMask(merged, m_selection);

    QVector<Layer*> keep;
    keep.append(m_layers.last());
    qDeleteAll(m_layers);
    m_layers.clear();
    Layer* l = new Layer(m_width, m_height);
    l->setName(tr("Merged"));
    l->setSurface(merged);
    m_layers.append(l);
    m_activeLayer = 0;
    notifyLayerStructure();
    emit activeLayerChanged(m_activeLayer);
}

void Document::flattenImage()
{
    Surface merged(m_width, m_height);
    for (Layer* l : m_layers)
        Renderer::compositeLayer(l, merged);
    Renderer::applyMask(merged, m_selection);

    // "Flatten image" also discards the alpha of the background.
    Surface flat(merged.width(), merged.height());
    for (int y = 0; y < merged.height(); ++y) {
        const pixel_t* s = merged.scanLine(y);
        pixel_t* d = flat.scanLine(y);
        for (int x = 0; x < merged.width(); ++x)
            d[x] = qPremult(255, getR(s[x]), getG(s[x]), getB(s[x]));
    }

    qDeleteAll(m_layers);
    m_layers.clear();
    Layer* l = new Layer(m_width, m_height);
    l->setName(tr("Background"));
    l->setBackground(true);
    l->setSurface(flat);
    m_layers.append(l);
    m_activeLayer = 0;
    m_selection = Selection();
    notifyLayerStructure();
    emit selectionChanged();
    emit activeLayerChanged(m_activeLayer);
}

void Document::replaceLayerPixels(int index, const Surface& newPixels)
{
    Layer* l = layerAt(index);
    if (!l)
        return;
    Surface before = l->surface().copy();
    l->setSurface(newPixels.copy());
    l->markThumbnailDirty();
    m_history->push(new SurfaceAction(tr("Layer Properties"), index, before, l->surface()));
    notifyLayerPixels(index, l->bounds());
}

void Document::mergeSelectionIntoLayer()
{
    // Merges the selection into the layer below (Paint.NET behaviour).
    if (m_layers.size() < 2 || m_selection.isNull())
        return;
    int idx = qMin(m_activeLayer, m_layers.size() - 1);
    if (idx == 0)
        idx = 1;
    mergeDown(idx);
}

// ------------------------------------------------------------------ canvas

void Document::resizeCanvas(int w, int h, Qt::Alignment anchor, bool lockAspect)
{
    w = qMax(1, w);
    h = qMax(1, h);
    if (lockAspect && m_width > 0) {
        double ar = double(m_width) / m_height;
        if (w / double(h) > ar)
            w = int(h * ar + 0.5);
        else
            h = int(w / ar + 0.5);
        w = qMax(1, w);
        h = qMax(1, h);
    }
    if (w == m_width && h == m_height)
        return;

    int dx = 0, dy = 0;
    if (anchor & Qt::AlignLeft) dx = 0;
    else if (anchor & Qt::AlignRight) dx = w - m_width;
    else if (anchor & Qt::AlignHCenter) dx = (w - m_width) / 2;
    if (anchor & Qt::AlignTop) dy = 0;
    else if (anchor & Qt::AlignBottom) dy = h - m_height;
    else if (anchor & Qt::AlignVCenter) dy = (h - m_height) / 2;

    const int oldW = m_width, oldH = m_height;
    Selection selBefore = m_selection;
    m_history->beginMacro(tr("Canvas Size"));
    for (Layer* l : m_layers) {
        const Surface before = l->surface().copy();
        Surface after(w, h);
        after.copyFrom(before, QPoint(dx, dy), before.bounds());
        l->setSurface(after);
        l->markThumbnailDirty();
        m_history->push(new SurfaceAction(tr("Canvas Size"), m_layers.indexOf(l), before, after));
        notifyLayerPixels(m_layers.indexOf(l), l->bounds());
    }
    Selection ns;
    if (selBefore.isNull()) {
        ns = Selection();
    } else {
        QImage m2(w, h, QImage::Format_Alpha8);
        m2.fill(0);
        QPainter p(&m2);
        p.drawImage(dx, dy, selBefore.mask());
        ns.setMask(m2);
    }
    m_selection = ns;
    m_width = w;
    m_height = h;
    m_history->push(new CanvasBoundsAction(tr("Canvas Size"), oldW, oldH, selBefore, w, h, ns));
    m_history->endMacro();
    emit canvasSizeChanged(w, h);
    emit selectionChanged();
    notifyLayerStructure();
}

void Document::cropCanvas(const QRect& newBounds)
{
    QRect r = newBounds.intersected(bounds());
    if (r.isEmpty() || (r == bounds()))
        return;
    const int oldW = m_width, oldH = m_height;
    Selection selBefore = m_selection;
    m_history->beginMacro(tr("Crop Image"));
    for (Layer* l : m_layers) {
        const int idx = m_layers.indexOf(l);
        const Surface before = l->surface().copy();
        const Surface after = before.cropped(r);
        l->setSurface(after);
        l->markThumbnailDirty();
        m_history->push(new SurfaceAction(tr("Crop Image"), idx, before, after));
        notifyLayerPixels(idx, l->bounds());
    }
    Selection newSel(m_selection);
    if (!m_selection.isNull()) {
        QImage m2(r.width(), r.height(), QImage::Format_Alpha8);
        m2.fill(0);
        QPainter p(&m2);
        p.drawImage(-r.left(), -r.top(), m_selection.mask());
        newSel.setMask(m2);
    }
    const int w = r.width(), h = r.height();
    m_width = w;
    m_height = h;
    m_selection = newSel;
    m_history->push(new CanvasBoundsAction(tr("Crop Image"), oldW, oldH, selBefore, w, h, newSel));
    m_history->endMacro();
    emit canvasSizeChanged(w, h);
    emit selectionChanged();
    notifyLayerStructure();
}

void Document::setCanvasBoundsOnly(int w, int h, const Selection& sel)
{
    m_width = qMax(1, w);
    m_height = qMax(1, h);
    m_selection = sel;
    emit canvasSizeChanged(m_width, m_height);
    emit selectionChanged();
}

void Document::translateLayers(int dx, int dy)
{
    for (Layer* l : m_layers) {
        Surface ns(l->width(), l->height());
        ns.copyFrom(l->surface(), QPoint(dx, dy), l->bounds());
        l->setSurface(ns);
        l->markThumbnailDirty();
    }
}

void Document::centerLayer()
{
    Layer* l = activeLayer();
    if (!l)
        return;
    QRect content = l->bounds();
    // Bounding box of the non-transparent pixels.
    Surface comp(l->width(), l->height());
    Renderer::compositeLayer(l, comp);
    content = QRect();
    for (int y = 0; y < comp.height(); ++y) {
        const pixel_t* row = comp.scanLine(y);
        int minX = -1, maxX = -1;
        for (int x = 0; x < comp.width(); ++x) {
            if (getA(row[x]) != 0) {
                if (minX < 0)
                    minX = x;
                maxX = x;
            }
        }
        if (minX >= 0)
            content = content.isNull() ? QRect(minX, y, maxX - minX + 1, 1)
                                       : content.united(QRect(minX, y, maxX - minX + 1, 1));
    }
    if (content.isNull())
        return;
    const int dx = (m_width - content.width()) / 2 - content.left();
    const int dy = (m_height - content.height()) / 2 - content.top();

    m_history->beginMacro(tr("Center Layer"));
    for (int i = 0; i < layerCount(); ++i) {
        Layer* cur = layerAt(i);
        Surface before = cur->surface().copy();
        Surface after(m_width, m_height);
        after.copyFrom(before, QPoint(dx, dy), before.bounds());
        cur->setSurface(after);
        cur->markThumbnailDirty();
        m_history->push(new SurfaceAction(tr("Center Layer"), i, before, after));
        notifyLayerPixels(i, cur->bounds());
    }
    m_history->endMacro();
}

void Document::trimTransparent(bool keepSelection)
{
    QRect r;
    for (Layer* l : m_layers) {
        if (!l->visible())
            continue;
        Surface comp(l->width(), l->height());
        Renderer::compositeLayer(l, comp);
        for (int y = 0; y < comp.height(); ++y) {
            const pixel_t* row = comp.scanLine(y);
            int minX = -1, maxX = -1;
            for (int x = 0; x < comp.width(); ++x) {
                if (getA(row[x]) != 0) {
                    if (minX < 0)
                        minX = x;
                    maxX = x;
                }
            }
            if (minX >= 0)
                r = r.isNull() ? QRect(minX, y, maxX - minX + 1, 1) : r.united(QRect(minX, y, maxX - minX + 1, 1));
        }
    }
    if (r.isEmpty()) {
        r = QRect(0, 0, qMin(1, m_width), qMin(1, m_height));
    }
    cropCanvas(r);
    if (!keepSelection)
        deselect();
}

// ------------------------------------------------------------------ selection

void Document::setSelection(const Selection& s)
{
    m_selection = s;
    emit selectionChanged();
    setDirty(true);
}

QRect Document::selectionBounds() const
{
    if (m_selection.isNull())
        return bounds();
    return m_selection.nonEmptyRect();
}

void Document::selectAll()
{
    m_history->push(new SelectionAction(tr("Select All"), m_selection, Selection()));
    setSelection(Selection());
}

void Document::deselect()
{
    m_history->push(new SelectionAction(tr("Deselect"), m_selection, Selection()));
    setSelection(Selection());
}

void Document::invertSelection()
{
    Selection s = m_selection;
    s.invert();
    m_history->push(new SelectionAction(tr("Invert Selection"), m_selection, s));
    setSelection(s);
}

void Document::addToSelection(const Selection& s)
{
    Selection n = m_selection;
    n += s;
    m_history->push(new SelectionAction(tr("Add to Selection"), m_selection, n));
    setSelection(n);
}

void Document::subtractFromSelection(const Selection& s)
{
    Selection n = m_selection;
    n -= s;
    m_history->push(new SelectionAction(tr("Subtract From Selection"), m_selection, n));
    setSelection(n);
}

void Document::intersectSelection(const Selection& s)
{
    Selection n = m_selection;
    n &= s;
    m_history->push(new SelectionAction(tr("Intersect With Selection"), m_selection, n));
    setSelection(n);
}

void Document::intersectSelection(const QRect& r)
{
    Selection s = m_selection;
    s.intersect(r);
    m_history->push(new SelectionAction(tr("Intersect With Selection"), m_selection, s));
    setSelection(s);
}

void Document::featherSelection(int radius)
{
    Selection s = m_selection;
    s.feather(radius);
    m_history->push(new SelectionAction(tr("Feather Selection"), m_selection, s));
    setSelection(s);
}

void Document::growSelection(int amount)
{
    Selection s = m_selection;
    s.grow(amount);
    m_history->push(new SelectionAction(amount < 0 ? tr("Contract Selection")
                                                    : tr("Grow Selection"),
                                        m_selection, s));
    setSelection(s);
}

void Document::growSelectionOctaves(int amount)
{
    Selection s = m_selection;
    int step = amount;
    for (int i = 0; i < 4 && step > 0; ++i) {
        s.grow(step);
        step /= 2;
    }
    m_history->push(new SelectionAction(tr("Grow Selection"), m_selection, s));
    setSelection(s);
}

void Document::contractSelection(int amount)
{
    Selection s = m_selection;
    s.contract(amount);
    m_history->push(new SelectionAction(tr("Contract Selection"), m_selection, s));
    setSelection(s);
}

void Document::borderSelection(int width)
{
    Selection s = m_selection;
    s.border(width);
    m_history->push(new SelectionAction(tr("Border Selection"), m_selection, s));
    setSelection(s);
}

} // namespace pnq
