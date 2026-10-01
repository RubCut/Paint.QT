#include "io/RecentFiles.h"
#include "ui/MainWindow.h"

#include <QFileInfo>
#include <QSettings>

namespace pnq {

RecentFiles::RecentFiles(QObject* parent) : QObject(parent)
{
    QSettings s;
    m_max = s.value(QStringLiteral("recent/max"), 8).toInt();
    m_files = s.value(QStringLiteral("recent/files")).toStringList();
    m_files.removeAll(QString());
    m_files.removeDuplicates();
}

void RecentFiles::add(const QString& path)
{
    if (path.isEmpty())
        return;
    const QString canonical = QFileInfo(path).absoluteFilePath();
    m_files.removeAll(canonical);
    m_files.prepend(canonical);
    while (m_files.size() > m_max)
        m_files.removeLast();
    QSettings s;
    s.setValue(QStringLiteral("recent/files"), m_files);
    emit changed();
}

void RecentFiles::remove(const QString& path)
{
    if (m_files.removeAll(path) > 0) {
        QSettings s;
        s.setValue(QStringLiteral("recent/files"), m_files);
        emit changed();
    }
}

void RecentFiles::clear()
{
    m_files.clear();
    QSettings s;
    s.setValue(QStringLiteral("recent/files"), m_files);
    emit changed();
}

void RecentFiles::setMaxCount(int n)
{
    m_max = qBound(1, n, 20);
    while (m_files.size() > m_max)
        m_files.removeLast();
    QSettings s;
    s.setValue(QStringLiteral("recent/max"), m_max);
    emit changed();
}

void RecentFiles::openFile(const QString& path)
{
    MainWindow* w = new MainWindow();
    w->show();
    w->openFile(path);
}

} // namespace pnq
