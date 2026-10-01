#pragma once

#include "core/History.h"

#include <QAbstractTableModel>
#include <QWidget>

class QTableView;

namespace pnq {

class HistoryTableModel : public QAbstractTableModel
{
    Q_OBJECT
public:
    explicit HistoryTableModel(QObject* parent = nullptr);
    void setHistory(History* h);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation o, int role) const override;
    void reset() { beginResetModel(); endResetModel(); }

private:
    History* m_history = nullptr;
};

/// History palette: icon, action name and the "current position" pointer.
class HistoryPane : public QWidget
{
    Q_OBJECT
public:
    explicit HistoryPane(QWidget* parent = nullptr);

    void setHistory(History* h);

signals:
    void jumpRequested(int index);

private slots:
    void onClicked(const QModelIndex& index);
    void onChanged();

private:
    HistoryTableModel* m_model = nullptr;
    QTableView* m_view = nullptr;
    History* m_history = nullptr;
    bool m_syncing = false;
};

} // namespace pnq
