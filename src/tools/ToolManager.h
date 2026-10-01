#pragma once

#include "tools/Tool.h"

#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

namespace pnq {

/// Owns every tool instance and resolves shortcuts.
class ToolManager : public QObject
{
    Q_OBJECT
public:
    explicit ToolManager(QObject* parent = nullptr);
    ~ToolManager() override;

    QList<Tool*> tools() const { return m_tools; }
    Tool* byId(const QString& id) const;
    Tool* byIndex(int index) const;
    int indexOf(Tool* t) const;
    int count() const { return m_tools.size(); }

    Tool* active() const { return m_active; }
    void setActive(Tool* t);
    bool setActiveById(const QString& id);

    /// Finds a tool by its shortcut string (e.g. "B", "Ctrl+M").
    Tool* byShortcut(const QString& shortcut) const;

    void setDocument(class Document* d);
    void setPrimaryColor(pixel_t c);
    void setSecondaryColor(pixel_t c);
    void setBrush(const Brush& b);

signals:
    void activeToolChanged(Tool* tool);
    void toolActivated(Tool* tool);

private:
    QList<Tool*> m_tools;
    Tool* m_active = nullptr;
};

// Factories implemented by the individual tool translation units.
QList<Tool*> createDrawingTools();    // DrawingTools.cpp
QList<Tool*> createFillTools();       // FillTools.cpp
QList<Tool*> createSelectionTools();  // SelectionTools.cpp
QList<Tool*> createShapeTools();      // ShapeTools.cpp
QList<Tool*> createTransformTools();  // TransformTools.cpp
QList<Tool*> createTextTools();       // TextTool.cpp

} // namespace pnq
