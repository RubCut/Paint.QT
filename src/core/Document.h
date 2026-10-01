#pragma once

#include "core/BlendMode.h"
#include "core/ColorUtils.h"
#include "core/Layer.h"
#include "core/Selection.h"
#include "core/Surface.h"

#include <QFileInfo>
#include <QImage>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QVector>

namespace pnq {

class History;

/// The document model: a stack of layers plus a selection, with undo support.
class Document : public QObject
{
    Q_OBJECT
public:
    Document(int width, int height, QObject* parent = nullptr);
    ~Document() override;

    // ------------------------------------------------------------- geometry
    int width() const { return m_width; }
    int height() const { return m_height; }
    QSize size() const { return QSize(m_width, m_height); }
    QRect bounds() const { return QRect(0, 0, m_width, m_height); }

    /// Resizes the canvas; anchor is one of the Qt::Anchor* flags.
    void resizeCanvas(int w, int h, Qt::Alignment anchor = Qt::AlignCenter, bool lockAspect = false);
    void cropCanvas(const QRect& newBounds);
    void translateLayers(int dx, int dy);
    /// Moves every layer so that the active one is centred on the canvas.
    void centerLayer();
    /// Sets size + selection without touching layer pixels (used by history).
    void setCanvasBoundsOnly(int w, int h, const Selection& sel);
    void flattenImage();
    void trimTransparent(bool keepSelection = true);

    // ------------------------------------------------------------- layers
    const QVector<Layer*>& layers() const { return m_layers; }
    int layerCount() const { return m_layers.size(); }
    Layer* layerAt(int index) const;
    Layer* activeLayer() const { return m_activeLayer >= 0 && m_activeLayer < m_layers.size()
                                     ? m_layers[m_activeLayer]
                                     : nullptr; }
    int activeLayerIndex() const { return m_activeLayer; }
    void setActiveLayerIndex(int index);

    Layer* addLayer(const QString& name = QString(), int index = -1);
    Layer* insertLayer(Layer* layer, int index);
    void removeLayerAt(int index);
    Layer* duplicateLayerAt(int index);
    bool moveLayer(int from, int to);
    /// Wraps moveLayer with a QAbstractItemModel::beginMoveRows, for view models.
    bool beginMoveLayer(int from, int to);
    void mergeDown(int index);
    void mergeVisible();
    void mergeSelectionIntoLayer();  // Paint.NET: flatten selection onto the layer below
    void replaceLayerPixels(int index, const Surface& newPixels);

    // ------------------------------------------------------------- selection
    const Selection& selection() const { return m_selection; }
    Selection& selection() { return m_selection; }
    void setSelection(const Selection& s);
    bool hasSelection() const { return !m_selection.isNull() && !m_selection.isEmpty(); }
    QRect selectionBounds() const;
    void selectAll();
    void deselect();
    void invertSelection();
    void addToSelection(const Selection& s);
    void subtractFromSelection(const Selection& s);
    void intersectSelection(const Selection& s);
    void intersectSelection(const QRect& r);
    void featherSelection(int radius);
    void growSelection(int amount);
    void growSelectionOctaves(int amount);
    void contractSelection(int amount);
    void borderSelection(int width);

    // ------------------------------------------------------------- history
    History* history() const { return m_history; }
    void setMaxHistoryLength(int n);
    int maxHistoryLength() const;

    // ------------------------------------------------------------- file state
    QString filePath() const { return m_filePath; }
    void setFilePath(const QString& p) { m_filePath = p; }
    bool isDirty() const { return m_dirty; }
    void setDirty(bool d);
    void markSaved();

    // ------------------------------------------------------------- misc
    QImage compositeImage() const;
    Surface compositeSurface() const;
    QImage layerThumbnail(int index, int maxSize) const;
    QString displayName() const;

    /// Combined dirty region helper used by tools.
    QRect clippedToCanvas(const QRect& r) const;

signals:
    void layersChanged();
    void activeLayerChanged(int index);
    void layerPropertiesChanged(int index);
    void layerPixelsChanged(int index, const QRect& rect);
    void layerStructureChanged();
    void selectionChanged();
    void canvasSizeChanged(int w, int h);
    void dirtyChanged(bool dirty);
    void historyChanged();

public:
    /// Called by history items.
    void notifyLayerPixels(int index, const QRect& r);
    void notifyLayerStructure();

private:
    int m_width = 0;
    int m_height = 0;
    QVector<Layer*> m_layers;
    int m_activeLayer = 0;
    Selection m_selection;
    History* m_history = nullptr;
    QString m_filePath;
    bool m_dirty = false;
    int m_maxHistory = 20;
};

} // namespace pnq
