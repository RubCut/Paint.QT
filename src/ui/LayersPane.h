#pragma once

#include "core/Document.h"

#include <QAbstractItemModel>
#include <QWidget>

class QListView;
class QListWidget;
class QSlider;
class QFrame;
class QLabel;
class QComboBox;
class QToolButton;
class QMenu;

namespace pnq {

/// List model over the document layers (index 0 = bottom).
class LayersModel : public QAbstractListModel
{
    Q_OBJECT
public:
    explicit LayersModel(Document* doc, QObject* parent = nullptr);
    void setDocument(Document* d);
    void refresh();
    /// Appends a layer, emitting proper rowsInserted so that a drag can reorder it.
    void appendLayer(Layer* layer);
    /// Inserts at an index (top of the stack).
    void insertLayerAt(Layer* layer, int index);
    /// Removes the layer at `index` from the document (and notifies).
    void removeLayerAt(int index);
    void setLayerVisible(int index, bool visible);
    void renameLayer(int index, const QString& name);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    Qt::ItemFlags flags(const QModelIndex& index) const override;
    bool setData(const QModelIndex& index, const QVariant& value, int role = Qt::EditRole) override;
    QVariant headerData(int section, Qt::Orientation o, int role) const override;
    Qt::DropActions supportedDropActions() const override;
    bool removeRows(int row, int count, const QModelIndex& parent = QModelIndex()) override;
    QStringList mimeTypes() const override;
    QMimeData* mimeData(const QModelIndexList& indexes) const override;
    bool dropMimeData(const QMimeData* data, Qt::DropAction action, int row, int column,
                      const QModelIndex& parent) override;

    void moveRows(int from, int to);
    int rowOfLayer(int layerIndex) const { return layerIndex; }
    Document* document() const { return m_doc; }

    void refreshThumbnail(int layerIndex);

private:
    Document* m_doc = nullptr;
    mutable QHash<int, QImage> m_thumbs;
    mutable int m_thumbSize = 40;
};

/// The Layers palette: list, blend mode, opacity, and the layer menu.
class LayersPane : public QWidget
{
    Q_OBJECT
public:
    explicit LayersPane(QWidget* parent = nullptr);

    void setDocument(Document* doc);
    Document* document() const { return m_doc; }
    void refresh();
    void setActiveIndex(int index);

signals:
    void activeLayerChanged(int index);
    void selectionChanged(int index);
    void layerMenuRequested(const QPoint& globalPos, int index);
    void statusMessage(const QString& text);
    void layerPropertiesRequested(int index);
    void layerOrderChanged(int from, int to);

public slots:
    void addLayer();
    void deleteLayer();
    void duplicateLayer();
    void mergeDown();
    void mergeVisible();
    void flattenImage();
    void showLayerPropertiesDialog();
    /// Sends a "move layer" reorder request (used by drag & drop).
    void requestMoveLayer(int from, int to);
    void moveLayerUp();
    void moveLayerDown();
    void selectAllLayers();
    void invertSelection();
    void showLayerProperties();

private:
    void updatePropertiesUi();
    void buildMenu();
    QMenu* menu() const { return m_menu; }

    Document* m_doc = nullptr;
    LayersModel* m_model = nullptr;
    QListView* m_view = nullptr;
    QSlider* m_opacity = nullptr;
    QLabel* m_opacityLabel = nullptr;
    QComboBox* m_mode = nullptr;
    QToolButton* m_moreButton = nullptr;
    QMenu* m_menu = nullptr;
    int m_updating = 0;
};

} // namespace pnq
