#include "ui/HistoryPane.h"
#include "resources/Icons.h"

#include <QHeaderView>
#include <QTableView>
#include <QVBoxLayout>

namespace pnq {

HistoryTableModel::HistoryTableModel(QObject* parent) : QAbstractTableModel(parent) {}

void HistoryTableModel::setHistory(History* h)
{
    beginResetModel();
    m_history = h;
    endResetModel();
}

int HistoryTableModel::rowCount(const QModelIndex& parent) const
{
    if (parent.isValid() || !m_history)
        return 0;
    return m_history->count() + 1; ///< + the "image opened" root entry
}

int HistoryTableModel::columnCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : 2;
}

QVariant HistoryTableModel::data(const QModelIndex& index, int role) const
{
    if (!m_history || !index.isValid())
        return QVariant();
    const int row = index.row();
    const int actionIndex = row - 1;
    if (row == 0) {
        if (index.column() == 0)
            return tr("Image opened");
        return QVariant();
    }
    if (actionIndex < 0 || actionIndex >= m_history->count())
        return QVariant();
    if (index.column() == 0)
        return m_history->actionNames().value(actionIndex);
    return QVariant();
}

QVariant HistoryTableModel::headerData(int, Qt::Orientation, int) const
{
    return QVariant();
}

// ------------------------------------------------------------------

HistoryPane::HistoryPane(QWidget* parent) : QWidget(parent)
{
    m_model = new HistoryTableModel(this);
    m_view = new QTableView(this);
    m_view->setModel(m_model);
    m_view->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_view->setSelectionMode(QAbstractItemView::SingleSelection);
    m_view->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_view->verticalHeader()->setVisible(false);
    m_view->horizontalHeader()->setVisible(false);
    m_view->horizontalHeader()->setStretchLastSection(true);
    m_view->setColumnWidth(0, 150);
    m_view->setShowGrid(false);
    m_view->setMinimumWidth(180);

    QVBoxLayout* l = new QVBoxLayout(this);
    l->setContentsMargins(2, 2, 2, 2);
    l->addWidget(m_view);

    connect(m_view, &QTableView::clicked, this, &HistoryPane::onClicked);
}

void HistoryPane::setHistory(History* h)
{
    if (m_history)
        m_history->disconnect(this);
    m_history = h;
    m_model->setHistory(h);
    if (h) {
        connect(h, &History::changed, this, &HistoryPane::onChanged);
        onChanged();
    }
}

void HistoryPane::onClicked(const QModelIndex& index)
{
    if (!m_history)
        return;
    m_syncing = true;
    m_view->clearSelection();
    m_view->selectRow(index.row());
    m_syncing = false;
    emit jumpRequested(index.row() - 1);
}

void HistoryPane::onChanged()
{
    if (!m_history)
        return;
    m_syncing = true;
    m_model->reset();
    const int row = m_history->currentIndex() + 1;
    if (row >= 0 && row < m_model->rowCount()) {
        m_view->selectRow(row);
        m_view->scrollTo(m_model->index(row, 0), QAbstractItemView::PositionAtCenter);
    }
    m_syncing = false;
}

} // namespace pnq
