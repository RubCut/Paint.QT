#pragma once

#include "core/Brush.h"
#include "core/ColorUtils.h"
#include "core/Document.h"
#include "core/Selection.h"
#include "core/Surface.h"

#include <QImage>
#include <QKeyEvent>
#include <QObject>
#include <QPainter>
#include <QPolygon>
#include <QString>
#include <QVector>

class QWidget;

namespace pnq {

class CanvasView;

/// Base class of every tool. The canvas feeds document-space coordinates;
/// the painter handed to drawOverlay() already has the image transform applied.
class Tool : public QObject
{
    Q_OBJECT
public:
    explicit Tool(QObject* parent = nullptr);
    ~Tool() override;

    // ------------------------------------------------------------- identity
    virtual QString id() const = 0;
    virtual QString name() const = 0;
    virtual QString toolTip() const { return name(); }
    virtual QString statusText() const { return QString(); }
    virtual QIcon icon() const;
    virtual QString shortcutString() const { return QString(); }
    /// Order in the toolbar / tool list.
    virtual int sortOrder() const { return 0; }

    // ------------------------------------------------------------- lifecycle
    virtual void activate();
    virtual void deactivate();

    /// Commits whatever the tool is still collecting: a multi-point shape, an
    /// in-progress gradient or transform. Returns true when something was
    /// finished, which is what the Tool Options "Finish" button keys off.
    virtual bool finishSession() { return false; }
    bool isActive() const { return m_active; }

    // ------------------------------------------------------------- input
    virtual void mouseDown(const QPoint& docPos, Qt::MouseButton button, Qt::KeyboardModifiers mods);
    virtual void mouseMove(const QPoint& docPos, Qt::MouseButtons buttons, Qt::KeyboardModifiers mods);
    virtual void mouseUp(const QPoint& docPos, Qt::MouseButton button, Qt::KeyboardModifiers mods);
    /// Sub-pixel variant used while the button is held; defaults to mouseMove().
    virtual void mouseDrag(const QPointF& docPos, Qt::MouseButtons buttons, Qt::KeyboardModifiers mods);
    virtual void mouseDoubleClick(const QPoint& docPos, Qt::MouseButton button,
                                  Qt::KeyboardModifiers mods);
    virtual void wheelEvent(QPoint delta, Qt::KeyboardModifiers mods);
    virtual void keyPressEvent(QKeyEvent* e);
    virtual void keyReleaseEvent(QKeyEvent* e);

    /// Tool draws its own cursor (crosshair / move / zoom ...).
    virtual Qt::CursorShape cursorShape() const { return Qt::CrossCursor; }
    /// The tool wants a preview layer instead of painting immediately.
    virtual bool isPreviewMode() const { return false; }

    // ------------------------------------------------------------- options UI
    virtual QWidget* createOptionsWidget(QWidget* parent);
    /// Re-creates the options widget (after the tool got a new document).
    virtual void refreshOptionsWidget();
    QWidget* optionsWidget() const { return m_optionsWidget; }

    // ------------------------------------------------------------- painting
    /// Overlay is drawn in image coordinates.
    virtual void drawOverlay(QPainter& p);

    // ------------------------------------------------------------- state
    Document* document() const { return m_doc; }
    void setDocument(Document* d);
    CanvasView* view() const { return m_view; }
    void setView(CanvasView* v) { m_view = v; }

    Brush& brush() { return m_brush; }
    const Brush& brush() const { return m_brush; }
    void setBrush(const Brush& b);

    pixel_t primaryColor() const { return m_primary; }
    pixel_t secondaryColor() const { return m_secondary; }
    void setPrimaryColor(pixel_t c) { m_primary = c; }
    void setSecondaryColor(pixel_t c) { m_secondary = c; }
    /// The colour to paint with for the given mouse button.
    pixel_t colorForButton(Qt::MouseButton button) const;

    /// True while the right mouse button is held (secondary colour mode).
    bool usingSecondary() const { return m_secondaryMode; }

    // ------------------------------------------------------------- helpers
    /// Layer index of the active layer (or -1).
    int activeLayerIndex() const;
    Layer* activeLayer() const;
    /// False when the layer is locked or hidden.
    bool canPaint() const;
    bool isSelectionConstrained() const;
    const Selection* selectionOrNull() const;

    /// Adds a pixel delta action to the history stack.
    void pushPixelAction(const QString& name, int layerIndex, const QRect& rect, const Surface& before,
                         const Surface& after);
    void pushSurfaceAction(const QString& name, int layerIndex, const Surface& before,
                           const Surface& after);
    void pushSelectionAction(const QString& name, const Selection& before, const Selection& after);

    void requestUpdate(const QRect& docRect);
    void requestOverlayUpdate();
    void refreshStatus();

    void setCurrentPath(const QVector<QPoint>& pts) { m_currentPath = pts; }
    const QVector<QPoint>& currentPath() const { return m_currentPath; }
    void clearPath() { m_currentPath.clear(); }

    /// Emits toolChanged() so the UI can refresh the options bar.
signals:
    void optionsChanged();
    void statusChanged();
    void toolDeactivated();
    /// Asks the canvas to repaint this (document-space) region.
    void updateRequested(const QRect& docRect);
    void overlayUpdateRequested();
    void cursorMoved(const QPoint& docPos);
    void colorsUsed(quint32 primary, quint32 secondary);
    /// Emitted while the tool is mid-session (a multi-point shape, a gradient
    /// or a transform that has not been committed yet). Drives the Finish button.
    void sessionChanged(bool active);

protected:
    Document* m_doc = nullptr;
    CanvasView* m_view = nullptr;
    Brush m_brush;
    pixel_t m_primary = 0xFF000000u;
    pixel_t m_secondary = 0xFFFFFFFFu;
    bool m_active = false;
    bool m_secondaryMode = false;
    QVector<QPoint> m_currentPath;
    QWidget* m_optionsWidget = nullptr;
};

} // namespace pnq
