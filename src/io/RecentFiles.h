#pragma once

#include <QObject>
#include <QStringList>

namespace pnq {

/// Recently opened/saved files, persisted in QSettings.
class RecentFiles : public QObject
{
    Q_OBJECT
public:
    explicit RecentFiles(QObject* parent = nullptr);

    void add(const QString& path);
    void remove(const QString& path);
    void clear();
    QStringList files() const { return m_files; }
    int maxCount() const { return m_max; }
    void setMaxCount(int n);
    /// Creates a MainWindow for a path (forward declared to avoid a cycle).
    void openFile(const QString& path);

    static QString entryName(int i) { return QStringLiteral("recent/file%1").arg(i); }

signals:
    void changed();

private:
    QStringList m_files;
    int m_max = 8;
};

} // namespace pnq
