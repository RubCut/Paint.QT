#pragma once

#include "core/Document.h"
#include "core/Selection.h"

#include <QPointF>
#include <QRectF>
#include <QWidget>

class QPainter;
class QScrollBar;
class QTimer;
class QToolButton;

namespace pnq {

class Tool;
class ToolManager;
class Gradient;

/// The image editing surface: zoom, pan, rulers, selection ants, tool overlay.
class CanvasView : public QWidget
{
    Q_OBJECT
public:
    explicit CanvasView(QWidget* parent = nullptr);
    ~CanvasView() override;

    void setDocument(Document* doc);
    Document* document() const { return m_doc; }
    void setToolManager(ToolManager* tm);
    Tool* activeTool() const;
    void setActiveTool(Tool* t);

    // ------------------------------------------------------------- zoom/pan
    double zoom() const { return m_zoom; }
    void setZoom(double z, const QPointF& anchorInWidget = QPointF());
    void zoomIn();
    void zoomOut();
    void zoomToFit();
    void zoomToSelection();
    void zoomOriginal();
    void setZoomPercent(int percent);

    // ------------------------------------------------------------- transforms
    /// Image -> widget coordinates.
    QPointF imageToWidget(const QPointF& p) const;
    QPoint widgetToImage(const QPoint& p) const;
    QPointF widgetToImageF(const QPointF& p) const;
    QRect imageRectToWidget(const QRect& r) const;
    /// Visible widget rectangle.
    QRectF viewRect() const;
    /// Scrolls so that the given widget point stays put.
    void scrollTo(const QPointF& widgetPoint, const QPointF& imagePoint);

    // ------------------------------------------------------------- decoration
    bool rulersVisible() const { return m_rulers; }
    void setRulersVisible(bool v);
    bool gridVisible() const { return m_grid; }
    void setGridVisible(bool v) { m_grid = v; update(); }
    bool guidesVisible() const { return m_guides; }
    void setGuidesVisible(bool v) { m_guides = v; update(); }
    bool snapToGuides() const { return m_snap; }
    void setSnapToGuides(bool v) { m_snap = v; }
    bool gridSnapEnabled() const { return m_gridSnap; }
    void setGridSnapEnabled(bool v) { m_gridSnap = v; }
    int gridSize() const { return m_gridSize; }
    void setGridSize(int v) { m_gridSize = qMax(2, v); update(); }
    QColor gridColor() const { return m_gridColor; }
    void setGridColor(const QColor& c) { m_gridColor = c; update(); }

    void addHorizontalGuide(int y);
    void addVerticalGuide(int x);
    void removeHorizontalGuide(int y);
    void removeVerticalGuide(int x);
    void clearGuides();
    QList<int> horizontalGuides() const { return m_hGuides; }
    QList<int> verticalGuides() const { return m_vGuides; }

    // ------------------------------------------------------------- status
    QPoint lastCursorImagePos() const { return m_lastCursor; }
    bool isPanning() const { return m_panning; }
    bool spacePanning() const { return m_spaceDown; }

    /// Cached composite of the document; refreshed lazily.
    QImage compositeImage();
    /// Marks document-space pixels as needing a refresh.
    void invalidateTiles(const QRect& docRect);

    /// The gradient currently used by the gradient tool.
    void setActiveGradient(const Gradient* g) { m_gradient = g; }
    const Gradient* activeGradient() const { return m_gradient; }

    /// Programmatic cursor position (used by tests / the color picker).
    /// Suspends the marching-ants animation while the window is being moved.
    void setAnimationsEnabled(bool on);
    void setCursorImagePos(const QPoint& p);;

signals:
    void zoomChanged(double zoom);
    void cursorMoved(const QPoint& imagePos);
    /// Emitted when the widget's own size changes, so the palettes that are
    /// pinned to its corners can follow it.
    void canvasResized();
    void statusMessage(const QString& text);
    void colorPicked(quint32 primary, quint32 secondary);
    void rulerContextMenu(const QPoint& pos, bool horizontal);

protected:
    void paintEvent(QPaintEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void mouseDoubleClickEvent(QMouseEvent* e) override;
    void wheelEvent(QWheelEvent* e) override;
    void keyPressEvent(QKeyEvent* e) override;
    void keyReleaseEvent(QKeyEvent* e) override;
    void resizeEvent(QResizeEvent* e) override;
    void enterEvent(QEnterEvent* e) override;
    void leaveEvent(QEvent* e) override;

private slots:
    void onAntsTimer();
    void onDocumentChanged();

private:
    QRect rulerRect() const;
    void updateRulers(const QRect& r);
    void drawCheckerboard(QPainter& p, const QRectF& r) const;
    void drawSelectionAnts(QPainter& p) const;
    void drawOverlay(QPainter& p) const;
    void drawGuides(QPainter& p) const;
    QPointF snapPoint(const QPointF& p) const;
    void updateCursorPos(const QPoint& widgetPos);
    void onImageChanged(int layerIndex, const QRect& docRect);

    Document* m_doc = nullptr;
    ToolManager* m_tools = nullptr;
    Tool* m_tool = nullptr;
    const Gradient* m_gradient = nullptr;

    double m_zoom = 1.0;
    QPointF m_scroll; ///< top-left of the image in widget coordinates
    QPoint m_lastCursor;
    QPoint m_panAnchor;
    bool m_panning = false;
    bool m_spaceDown = false;
    Qt::MouseButton m_panButton = Qt::NoButton;

    bool m_rulers = true;
    bool m_grid = false;
    int m_gridSize = 32;
    bool m_guides = true;
    bool m_snap = true;
    bool m_gridSnap = false;
    QColor m_gridColor = QColor(128, 128, 128, 90);
    QList<int> m_hGuides;
    QList<int> m_vGuides;
    int m_antsOffset = 0;
    /// Button and modifiers the current stroke started with. Passing "no button"
    /// while dragging made every freehand tool draw a single straight line.
    Qt::MouseButtons m_dragButtons = Qt::NoButton;
    Qt::KeyboardModifiers m_dragMods = Qt::NoModifier;

    /// False while the window manager is dragging the window.
    bool m_animationsEnabled = true;

    /// False while the view should keep the image fitted to the window.
    bool m_userZoomed = false;

    QTimer* m_antsTimer = nullptr;
    // Cached composite of the document.
    mutable QImage m_compositeCache;
    QRect m_cacheRect;
    QRect m_dirtyRegion;
};

} // namespace pnq
