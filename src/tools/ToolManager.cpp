#include "tools/ToolManager.h"

#include <QIcon>
#include <algorithm>

namespace pnq {

ToolManager::ToolManager(QObject* parent) : QObject(parent)
{
    QList<Tool*> all;
    all += createDrawingTools();
    all += createSelectionTools();
    all += createShapeTools();
    all += createFillTools();
    all += createTransformTools();
    all += createTextTools();

    // Stable, Paint.NET-like ordering: selection tools last, zoom/pan at the end.
    std::sort(all.begin(), all.end(), [](Tool* a, Tool* b) {
        return a->sortOrder() < b->sortOrder();
    });
    m_tools = all;
}

ToolManager::~ToolManager()
{
    qDeleteAll(m_tools);
}

Tool* ToolManager::byId(const QString& id) const
{
    for (Tool* t : m_tools) {
        if (t->id() == id)
            return t;
    }
    return nullptr;
}

Tool* ToolManager::byIndex(int index) const
{
    if (index < 0 || index >= m_tools.size())
        return nullptr;
    return m_tools[index];
}

int ToolManager::indexOf(Tool* t) const
{
    return int(m_tools.indexOf(t));
}

Tool* ToolManager::byShortcut(const QString& shortcut) const
{
    if (shortcut.isEmpty())
        return nullptr;
    for (Tool* t : m_tools) {
        if (t->shortcutString().compare(shortcut, Qt::CaseInsensitive) == 0)
            return t;
    }
    return nullptr;
}

void ToolManager::setActive(Tool* t)
{
    if (!t || m_active == t)
        return;
    m_active = t;
    emit activeToolChanged(m_active);
    emit toolActivated(m_active);
}

bool ToolManager::setActiveById(const QString& id)
{
    Tool* t = byId(id);
    if (!t)
        return false;
    setActive(t);
    return true;
}

void ToolManager::setDocument(Document* d)
{
    for (Tool* t : m_tools)
        t->setDocument(d);
}

void ToolManager::setPrimaryColor(pixel_t c)
{
    for (Tool* t : m_tools)
        t->setPrimaryColor(c);
}

void ToolManager::setSecondaryColor(pixel_t c)
{
    for (Tool* t : m_tools)
        t->setSecondaryColor(c);
}

void ToolManager::setBrush(const Brush& b)
{
    for (Tool* t : m_tools)
        t->setBrush(b);
}

} // namespace pnq
