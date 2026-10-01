#include "ui/LayersPane.h"
#include "core/History.h"

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListView>
#include <QMenu>
#include <QMimeData>
#include <QPainter>
#include <QSlider>
#include <QToolButton>
#include <QVBoxLayout>

namespace pnq {

// ------------------------------------------------------------------ model

LayersModel::LayersModel(Document* doc, QObject* parent) : QAbstractListModel(parent)
{
    setDocument(doc);
}

void LayersModel::setDocument(Document* d)
{
    beginResetModel();
    m_doc = d;
    m_thumbs.clear();
    endResetModel();
}

void LayersModel::refresh()
{
    beginResetModel();
    m_thumbs.clear();
    endResetModel();
}

int LayersModel::rowCount(const QModelIndex& parent) const
{
    if (parent.isValid() || !m_doc)
        return 0;
    return m_doc->layerCount();
}

QVariant LayersModel::data(const QModelIndex& index, int role) const
{
    if (!m_doc || !index.isValid())
        return QVariant();
    Layer* l = m_doc->layerAt(index.row());
    if (!l)
        return QVariant();
    switch (role) {
    case Qt::DisplayRole:
        return l->name();
    case Qt::EditRole:
        return l->name();
    case Qt::ToolTipRole: {
        const QRect r = l->bounds();
        return tr("%1 - %2x%3 - %4%")
            .arg(l->name())
            .arg(r.width())
            .arg(r.height())
            .arg(int(l->opacity() * 100 / 255));
    }
    case Qt::DecorationRole: {
        if (!m_thumbs.contains(index.row())) {
            const int avail = 40 * int(qMax(1.0, double(qApp->devicePixelRatio())));
            m_thumbs.insert(index.row(), l->thumbnail(avail));
        }
        return m_thumbs.value(index.row());
    }
    case Qt::CheckStateRole:
        return l->visible() ? Qt::Checked : Qt::Unchecked;
    case Qt::UserRole:
        return l->isBackground() ? QLatin1String("background") : QString();
    default:
        break;
    }
    return QVariant();
}

Qt::ItemFlags LayersModel::flags(const QModelIndex& index) const
{
    Qt::ItemFlags f = QAbstractListModel::flags(index);
    if (index.isValid()) {
        f |= Qt::ItemIsEditable;
        f |= Qt::ItemIsUserCheckable;
        f |= Qt::ItemIsDragEnabled;
        f |= Qt::ItemIsDropEnabled;
    }
    f |= Qt::ItemIsDropEnabled;
    return f;
}

bool LayersModel::setData(const QModelIndex& index, const QVariant& value, int role)
{
    if (!m_doc || !index.isValid())
        return false;
    Layer* l = m_doc->layerAt(index.row());
    if (!l)
        return false;
    if (role == Qt::EditRole) {
        const QString name = value.toString();
        if (name.isEmpty() || name == l->name())
            return false;
        m_doc->history()->push(
            new LayerPropertiesAction(tr("Layer Properties"), index.row(), *l->clone(), *l->clone()));
        l->setName(name);
        emit dataChanged(index, index, { Qt::DisplayRole, Qt::EditRole });
        m_doc->notifyLayerStructure();
        return true;
    }
    if (role == Qt::CheckStateRole) {
        const bool vis = value.toInt() == Qt::Checked;
        if (vis == l->visible())
            return false;
        m_doc->history()->push(
            new LayerPropertiesAction(tr("Layer Properties"), index.row(), *l->clone(), *l->clone()));
        l->setVisible(vis);
        emit dataChanged(index, index, { Qt::CheckStateRole });
        m_doc->notifyLayerStructure();
        return true;
    }
    return false;
}

QVariant LayersModel::headerData(int, Qt::Orientation, int) const
{
    return QVariant();
}

bool LayersModel::removeRows(int row, int count, const QModelIndex& parent)
{
    if (parent.isValid() || count != 1 || !m_doc)
        return false;
    for (int i = 0; i < count; ++i)
        m_doc->removeLayerAt(row);
    return true;
}

void LayersModel::appendLayer(Layer* layer)
{
    if (!m_doc || !layer)
        return;
    const int at = m_doc->layerCount();
    beginInsertRows(QModelIndex(), at, at);
    m_doc->insertLayer(layer, at);
    endInsertRows();
}

void LayersModel::insertLayerAt(Layer* layer, int index)
{
    if (!m_doc || !layer)
        return;
    index = qBound(0, index, m_doc->layerCount());
    beginInsertRows(QModelIndex(), index, index);
    m_doc->insertLayer(layer, index);
    endInsertRows();
}

void LayersModel::removeLayerAt(int index)
{
    if (!m_doc)
        return;
    if (index < 0 || index >= m_doc->layerCount())
        return;
    beginRemoveRows(QModelIndex(), index, index);
    m_doc->removeLayerAt(index);
    endRemoveRows();
    m_thumbs.clear();
}

void LayersModel::setLayerVisible(int index, bool visible)
{
    if (!m_doc)
        return;
    Layer* l = m_doc->layerAt(index);
    if (!l)
        return;
    l->setVisible(visible);
    m_doc->notifyLayerStructure();
    const QModelIndex i = this->index(index);
    emit dataChanged(i, i, { Qt::CheckStateRole });
}

void LayersModel::renameLayer(int index, const QString& name)
{
    if (!m_doc)
        return;
    Layer* l = m_doc->layerAt(index);
    if (!l || name.isEmpty())
        return;
    l->setName(name);
    m_doc->notifyLayerStructure();
    const QModelIndex i = this->index(index);
    emit dataChanged(i, i, { Qt::DisplayRole, Qt::EditRole });
}

Qt::DropActions LayersModel::supportedDropActions() const
{
    return Qt::MoveAction;
}

QStringList LayersModel::mimeTypes() const
{
    return { QStringLiteral("application/x-paintnet-layer-index") };
}

QMimeData* LayersModel::mimeData(const QModelIndexList& indexes) const
{
    QMimeData* md = new QMimeData;
    QByteArray data;
    for (const QModelIndex& i : indexes) {
        if (i.isValid())
            data.append(QByteArray::number(i.row()));
    }
    md->setData(mimeTypes().first(), data);
    return md;
}

bool LayersModel::dropMimeData(const QMimeData* data, Qt::DropAction action, int, int,
                               const QModelIndex& parent)
{
    if (!m_doc || !data)
        return false;
    const QByteArray raw = data->data(mimeTypes().first());
    if (raw.isEmpty())
        return false;
    const int from = raw.toInt();
    // Drop position: the row the item was dropped on.
    int to = parent.isValid() ? parent.row() : m_doc->layerCount() - 1;
    if (to == from)
        return false;
    if (action == Qt::IgnoreAction)
        return true;
    // beginMoveRows expects (source row, count, destination child index).
    if (m_doc->beginMoveLayer(from, to)) {
        emit layoutChanged();
        return true;
    }
    return false;
}

void LayersModel::moveRows(int from, int to)
{
    if (!m_doc || from < 0 || from >= rowCount() || to < 0 || to >= rowCount() || from == to)
        return;
    if (!m_doc->beginMoveLayer(from, to))
        return;
    emit layoutChanged();
}

void LayersModel::refreshThumbnail(int layerIndex)
{
    if (layerIndex < 0 || layerIndex >= rowCount())
        return;
    m_thumbs.remove(layerIndex);
    const QModelIndex i = index(layerIndex);
    emit dataChanged(i, i, { Qt::DecorationRole });
}

// ------------------------------------------------------------------ pane

LayersPane::LayersPane(QWidget* parent) : QWidget(parent)
{
    m_model = new LayersModel(nullptr, this);

    m_view = new QListView(this);
    m_view->setModel(m_model);
    m_view->setViewMode(QListView::ListMode);
    m_view->setSelectionMode(QAbstractItemView::SingleSelection);
    m_view->setDragDropMode(QAbstractItemView::InternalMove);
    m_view->setDefaultDropAction(Qt::MoveAction);
    m_view->setDropIndicatorShown(true);
    m_view->setIconSize(QSize(40, 40));
    m_view->setUniformItemSizes(false);
    m_view->setContextMenuPolicy(Qt::CustomContextMenu);
    m_view->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::SelectedClicked);
    m_view->setMinimumWidth(190);
    connect(m_view, &QListView::clicked, this, [this](const QModelIndex& i) {
        if (i.isValid())
            emit selectionChanged(i.row());
    });
    connect(m_view, &QListView::activated, this, [this](const QModelIndex& i) { showLayerProperties(); });
    connect(m_view, &QWidget::customContextMenuRequested, this, [this](const QPoint& p) {
        const QModelIndex i = m_view->indexAt(p);
        if (i.isValid())
            emit layerMenuRequested(m_view->viewport()->mapToGlobal(p), i.row());
    });

    QHBoxLayout* propRow = new QHBoxLayout;
    m_mode = new QComboBox(this);
    for (BlendMode m : allBlendModes()) {
        m_mode->addItem(QString::fromLatin1(blendModeName(m)), int(m));
        if (blendModeStartsGroup(m) && int(m) > 0)
            m_mode->insertSeparator(m_mode->count() - 1);
    }
    connect(m_mode, &QComboBox::currentIndexChanged, this, [this] {
        if (m_updating || !m_doc || m_mode->currentIndex() < 0)
            return;
        const int row = m_view->currentIndex().row();
        Layer* l = m_doc->layerAt(row);
        if (!l)
            return;
        const auto mode = static_cast<BlendMode>(m_mode->currentData().toInt());
        if (mode == l->blendMode())
            return;
        Layer after = *l->clone();
        l->setBlendMode(mode);
        m_doc->history()->push(
            new LayerPropertiesAction(tr("Layer Properties"), row, after, *l->clone()));
        m_doc->notifyLayerStructure();
    });
    propRow->addWidget(m_mode, 1);

    m_opacity = new QSlider(Qt::Horizontal, this);
    m_opacity->setRange(0, 255);
    m_opacity->setValue(255);
    m_opacity->setMaximumWidth(90);
    connect(m_opacity, &QSlider::valueChanged, this, [this](int v) {
        if (m_updating || !m_doc)
            return;
        const int row = m_view->currentIndex().row();
        Layer* l = m_doc->layerAt(row);
        if (!l)
            return;
        l->setOpacity(v);
        if (m_opacityLabel)
            m_opacityLabel->setText(QStringLiteral("%1%").arg(qRound(v * 100.0 / 255)));
        emit statusMessage(tr("Opacity: %1%").arg(qRound(v * 100.0 / 255)));
    });
    m_opacityLabel = new QLabel(QStringLiteral("100%"), this);
    m_opacityLabel->setMinimumWidth(34);
    propRow->addWidget(new QLabel(tr("Opacity:"), this));
    propRow->addWidget(m_opacity);
    propRow->addWidget(m_opacityLabel);

    m_moreButton = new QToolButton(this);
    m_moreButton->setText(QStringLiteral("..."));
    m_moreButton->setPopupMode(QToolButton::InstantPopup);
    connect(m_moreButton, &QToolButton::clicked, this, [this] { m_menu->popup(mapToGlobal(
                                                                   QPoint(width() - 30, 0))); });

    QHBoxLayout* btnRow = new QHBoxLayout;
    struct { const char* label; const char* tip; void (LayersPane::*fn)(); } buttons[] = {
        { "+", QT_TRANSLATE_NOOP("LayersPane", "Add New Layer"), &LayersPane::addLayer },
        { "-", QT_TRANSLATE_NOOP("LayersPane", "Delete Layer"), &LayersPane::deleteLayer },
        { "D", QT_TRANSLATE_NOOP("LayersPane", "Duplicate Layer"), &LayersPane::duplicateLayer },
        { "\xe2\x86\x91", QT_TRANSLATE_NOOP("LayersPane", "Move Layer Up"), &LayersPane::moveLayerUp },
        { "\xe2\x86\x93", QT_TRANSLATE_NOOP("LayersPane", "Move Layer Down"), &LayersPane::moveLayerDown },
    };
    for (auto& b : buttons) {
        QToolButton* tb = new QToolButton(this);
        tb->setText(QString::fromUtf8(b.label));
        tb->setToolTip(tr(b.tip));
        connect(tb, &QToolButton::clicked, this, b.fn);
        btnRow->addWidget(tb);
    }
    btnRow->addStretch(1);
    btnRow->addWidget(m_moreButton);

    QVBoxLayout* l = new QVBoxLayout(this);
    l->setContentsMargins(2, 2, 2, 2);
    l->addWidget(m_view, 1);
    l->addLayout(propRow);
    l->addLayout(btnRow);

    buildMenu();
}

void LayersPane::buildMenu()
{
    m_menu = new QMenu(this);
    auto add = [&](const QString& text, void (LayersPane::*fn)()) {
        QAction* a = m_menu->addAction(text);
        connect(a, &QAction::triggered, this, fn);
    };
    add(tr("Add New Layer"), &LayersPane::addLayer);
    add(tr("Delete Layer"), &LayersPane::deleteLayer);
    add(tr("Duplicate Layer"), &LayersPane::duplicateLayer);
    m_menu->addSeparator();
    add(tr("Merge Layer Down"), &LayersPane::mergeDown);
    add(tr("Merge Visible Layers"), &LayersPane::mergeVisible);
    add(tr("Flatten Image"), &LayersPane::flattenImage);
    m_menu->addSeparator();
    add(tr("Move Layer Up"), &LayersPane::moveLayerUp);
    add(tr("Move Layer Down"), &LayersPane::moveLayerDown);
    m_menu->addSeparator();
    add(tr("Select All Layers"), &LayersPane::selectAllLayers);
    add(tr("Invert Selection"), &LayersPane::invertSelection);
    m_menu->addSeparator();
    add(tr("Layer Properties..."), &LayersPane::showLayerProperties);
}

void LayersPane::setDocument(Document* doc)
{
    if (m_doc)
        m_doc->disconnect(this);
    m_doc = doc;
    m_model->setDocument(doc);
    if (doc) {
        connect(doc, &Document::layersChanged, this, &LayersPane::refresh);
        connect(doc, &Document::activeLayerChanged, this, &LayersPane::setActiveIndex);
        connect(doc, &Document::layerPixelsChanged, this, [this](int idx, const QRect&) {
            m_model->refreshThumbnail(idx);
        });
        connect(doc, &Document::layerPropertiesChanged, this, [this](int idx) {
            m_model->refreshThumbnail(idx);
            refresh();
        });
    }
    refresh();
}

void LayersPane::setActiveIndex(int index)
{
    if (index < 0 || index >= m_model->rowCount())
        return;
    m_updating++;
    m_view->setCurrentIndex(m_model->index(index));
    m_view->scrollTo(m_model->index(index), QAbstractItemView::EnsureVisible);
    m_updating--;
    updatePropertiesUi();
    emit activeLayerChanged(index);
}

void LayersPane::refresh()
{
    const int active = m_doc ? m_doc->activeLayerIndex() : 0;
    const int current = m_view->currentIndex().row();
    m_updating++;
    m_model->refresh();
    if (current >= 0 && current < m_model->rowCount())
        m_view->setCurrentIndex(m_model->index(current));
    m_updating--;
    if (m_doc)
        setActiveIndex(active);
    else
        updatePropertiesUi();
}

void LayersPane::updatePropertiesUi()
{
    m_updating++;
    Layer* l = m_doc ? m_doc->layerAt(m_view->currentIndex().row()) : nullptr;
    m_mode->setEnabled(l != nullptr);
    m_opacity->setEnabled(l != nullptr);
    if (l) {
        const int idx = m_mode->findData(int(l->blendMode()));
        m_mode->setCurrentIndex(idx >= 0 ? idx : 0);
        m_opacity->setValue(l->opacity());
        m_opacityLabel->setText(QStringLiteral("%1%").arg(qRound(l->opacity() * 100.0 / 255)));
    } else {
        m_mode->setCurrentIndex(0);
        m_opacity->setValue(255);
    }
    m_updating--;
}

void LayersPane::addLayer()
{
    if (!m_doc)
        return;
    m_doc->history()->beginMacro(tr("Add New Layer"));
    Layer* l = m_doc->addLayer();
    m_doc->history()->push(
        new LayerStructureAction(tr("Add New Layer"), LayerStructureAction::Kind::Add,
                                 m_doc->activeLayerIndex(), nullptr, l));
    m_doc->history()->endMacro();
    m_doc->setActiveLayerIndex(m_doc->layerCount() - 1);
    m_model->refresh();
    setActiveIndex(m_doc->activeLayerIndex());
    emit statusMessage(tr("Layer %1 added").arg(m_doc->layerCount()));
}

void LayersPane::deleteLayer()
{
    if (!m_doc || m_doc->layerCount() <= 1)
        return;
    const int idx = m_view->currentIndex().row();
    if (idx < 0)
        return;
    Layer* backup = m_doc->layerAt(idx)->clone();
    m_doc->removeLayerAt(idx);
    m_doc->history()->push(new LayerStructureAction(tr("Delete Layer"),
                                                     LayerStructureAction::Kind::Remove, idx, backup,
                                                     nullptr));
    m_model->refresh();
    refresh();
}

void LayersPane::duplicateLayer()
{
    if (!m_doc)
        return;
    const int idx = m_view->currentIndex().row();
    if (idx < 0)
        return;
    Layer* copy = m_doc->duplicateLayerAt(idx);
    m_doc->history()->push(new LayerStructureAction(tr("Duplicate Layer"),
                                                     LayerStructureAction::Kind::Add,
                                                     m_doc->activeLayerIndex(), nullptr, copy));
    m_model->refresh();
    refresh();
}

void LayersPane::mergeDown()
{
    if (!m_doc)
        return;
    const int idx = m_view->currentIndex().row();
    if (idx <= 0)
        return;
    m_doc->history()->beginMacro(tr("Merge Layer Down"));
    m_doc->mergeDown(idx);
    m_doc->history()->endMacro();
    m_model->refresh();
    refresh();
}

void LayersPane::mergeVisible()
{
    if (!m_doc)
        return;
    m_doc->mergeVisible();
    m_model->refresh();
    refresh();
}

void LayersPane::flattenImage()
{
    if (!m_doc)
        return;
    m_doc->flattenImage();
    m_model->refresh();
    refresh();
}

void LayersPane::moveLayerUp()
{
    if (!m_doc)
        return;
    const int idx = m_view->currentIndex().row();
    if (idx < 0 || idx >= m_doc->layerCount() - 1)
        return;
    m_model->moveRows(idx, idx + 1);
    setActiveIndex(idx + 1);
}

void LayersPane::moveLayerDown()
{
    if (!m_doc)
        return;
    const int idx = m_view->currentIndex().row();
    if (idx <= 0)
        return;
    m_model->moveRows(idx, idx - 1);
    setActiveIndex(idx - 1);
}

void LayersPane::selectAllLayers()
{
    if (m_view->selectionModel()) {
        m_view->selectAll();
    }
    if (m_doc) {
        m_doc->setActiveLayerIndex(m_doc->layerCount() - 1);
        refresh();
    }
}

void LayersPane::invertSelection()
{
    if (m_view->selectionModel() && !m_view->selectionModel()->selectedIndexes().isEmpty()) {
        QItemSelection sel = m_view->selectionModel()->selection();
        QItemSelection inv;
        for (int i = 0; i < m_model->rowCount(); ++i) {
            if (!sel.contains(m_model->index(i)))
                inv.select(m_model->index(i), m_model->index(i));
        }
        m_view->selectionModel()->select(inv, QItemSelectionModel::ClearAndSelect);
    }
}

void LayersPane::showLayerProperties()
{
    if (!m_doc)
        return;
    emit layerPropertiesRequested(m_view->currentIndex().row());
}

void LayersPane::showLayerPropertiesDialog()
{
    showLayerProperties();
}

void LayersPane::requestMoveLayer(int from, int to)
{
    m_model->moveRows(from, to);
}

} // namespace pnq
