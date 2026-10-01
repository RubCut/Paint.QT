#include "tools/Tool.h"
#include "core/History.h"
#include "resources/Icons.h"
#include "tools/ToolSupport.h"
#include "ui/CanvasView.h"

#include <QKeyEvent>
#include <QWidget>

namespace pnq {

Tool::Tool(QObject* parent) : QObject(parent) {}

Tool::~Tool() = default;

QIcon Tool::icon() const
{
    return Icons::tool(id());
}

void Tool::setDocument(Document* d)
{
    m_doc = d;
    refreshOptionsWidget();
}

void Tool::setBrush(const Brush& b)
{
    m_brush = b;
    emit optionsChanged();
}

void Tool::activate()
{
    m_active = true;
    emit optionsChanged();
    refreshStatus();
}

void Tool::deactivate()
{
    if (m_active) {
        m_active = false;
        emit toolDeactivated();
    }
}

void Tool::mouseDown(const QPoint&, Qt::MouseButton, Qt::KeyboardModifiers) {}
void Tool::mouseMove(const QPoint&, Qt::MouseButtons, Qt::KeyboardModifiers) {}
void Tool::mouseDrag(const QPointF& p, Qt::MouseButtons b, Qt::KeyboardModifiers m)
{
    mouseMove(QPoint(int(std::floor(p.x() + 0.5)), int(std::floor(p.y() + 0.5))), b, m);
}


void Tool::mouseDoubleClick(const QPoint&, Qt::MouseButton, Qt::KeyboardModifiers) {}
void Tool::mouseUp(const QPoint&, Qt::MouseButton, Qt::KeyboardModifiers) {}
void Tool::wheelEvent(QPoint, Qt::KeyboardModifiers) {}
void Tool::keyPressEvent(QKeyEvent*) {}
void Tool::keyReleaseEvent(QKeyEvent*) {}

QWidget* Tool::createOptionsWidget(QWidget*)
{
    return nullptr;
}

void Tool::refreshOptionsWidget()
{
    if (m_optionsWidget) {
        m_optionsWidget->deleteLater();
        m_optionsWidget = nullptr;
    }
}

void Tool::drawOverlay(QPainter&) {}


pixel_t Tool::colorForButton(Qt::MouseButton button) const
{
    if (button == Qt::RightButton)
        return m_secondary;
    return m_secondaryMode ? m_secondary : m_primary;
}

int Tool::activeLayerIndex() const
{
    return m_doc ? m_doc->activeLayerIndex() : -1;
}

Layer* Tool::activeLayer() const
{
    return m_doc ? m_doc->activeLayer() : nullptr;
}

bool Tool::canPaint() const
{
    Layer* l = activeLayer();
    if (!l)
        return false;
    if (!l->visible())
        return false;
    return l->lock() != LayerLock::All;
}

bool Tool::isSelectionConstrained() const
{
    return m_doc && !m_doc->selection().isNull();
}

const Selection* Tool::selectionOrNull() const
{
    if (!m_doc)
        return nullptr;
    return &m_doc->selection();
}

void Tool::pushPixelAction(const QString& name, int layerIndex, const QRect& rect,
                           const Surface& before, const Surface& after)
{
    if (!m_doc || rect.isEmpty())
        return;
    m_doc->history()->push(new PixelDeltaAction(name, layerIndex, rect, before, after));
}

void Tool::pushSurfaceAction(const QString& name, int layerIndex, const Surface& before,
                             const Surface& after)
{
    if (!m_doc)
        return;
    m_doc->history()->push(new SurfaceAction(name, layerIndex, before, after));
}

void Tool::pushSelectionAction(const QString& name, const Selection& before, const Selection& after)
{
    if (!m_doc)
        return;
    m_doc->history()->push(new SelectionAction(name, before, after));
}

void Tool::requestUpdate(const QRect& docRect)
{
    emit updateRequested(docRect);
}

void Tool::requestOverlayUpdate()
{
    emit overlayUpdateRequested();
}

void Tool::refreshStatus()
{
    emit statusChanged();
}

} // namespace pnq
