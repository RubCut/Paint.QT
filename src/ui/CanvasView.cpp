#include "ui/CanvasView.h"
#include "core/Gradient.h"
#include "core/History.h"
#include "core/Renderer.h"
#include "tools/Tool.h"
#include "tools/ToolManager.h"
#include "tools/ToolSupport.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QTimer>
#include <QWheelEvent>
#include <QtMath>

#include <cmath>

namespace pnq {

namespace {
constexpr int TileSize = 128;
constexpr int RulerSize = 20;

double zoomForIndex(int index)
{
    static const double steps[] = { 0.03125, 0.0625, 0.125, 0.25, 0.3333, 0.5,  0.6667, 1.0,
                                    2.0,     3.0,     4.0,    6.0,   8.0,    12.0, 16.0,  24.0,
                                    32.0,    48.0,    64.0,   96.0,  128.0,  192.0, 256.0, 400.0,
                                    512.0,   800.0,   1024.0, 2048.0 };
    const int n = int(sizeof(steps) / sizeof(steps[0]));
    if (index < 0)
        return steps[0];
    if (index >= n)
        return steps[n - 1];
    return steps[index];
}

int indexForZoom(double z)
{
    static const double steps[] = { 0.03125, 0.0625, 0.125, 0.25, 0.3333, 0.5,  0.6667, 1.0,
                                    2.0,     3.0,     4.0,    6.0,   8.0,    12.0, 16.0,  24.0,
                                    32.0,    48.0,    64.0,   96.0,  128.0,  192.0, 256.0, 400.0,
                                    512.0,   800.0,   1024.0, 2048.0 };
    const int n = int(sizeof(steps) / sizeof(steps[0]));
    // The largest step that still fits in z. Returning the first step instead
    // would make every zoom read as 3.125%, so pressing + or - would collapse the
    // image instead of stepping through the scale.
    int best = 0;
    for (int i = 0; i < n; ++i) {
        if (steps[i] > z)
            break;
        best = i;
    }
    return best;
}
} // namespace

CanvasView::CanvasView(QWidget* parent) : QWidget(parent)
{
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setAcceptDrops(true);
    setAttribute(Qt::WA_OpaquePaintEvent, false);
    setMinimumSize(160, 120);

    m_antsTimer = new QTimer(this);
    m_antsTimer->setInterval(120);
    connect(m_antsTimer, &QTimer::timeout, this, &CanvasView::onAntsTimer);
    // Started only when there is a selection to animate, so an idle window is
    // not damaged eight times a second.

    m_dirtyRegion = QRect(0, 0, 100000, 100000);
}

CanvasView::~CanvasView() = default;

void CanvasView::setDocument(Document* doc)
{
    if (m_doc == doc)
        return;
    if (m_doc)
        m_doc->disconnect(this);
    m_doc = doc;
    m_dirtyRegion = QRect();
    m_compositeCache = QImage();
    if (m_doc) {
        connect(m_doc, &Document::layerPixelsChanged, this,
                [this](int, const QRect& r) { invalidateTiles(r); });
        connect(m_doc, &Document::layerStructureChanged, this, [this] { invalidateTiles(QRect()); });
        connect(m_doc, &Document::layerPropertiesChanged, this,
                [this](int) { invalidateTiles(QRect()); });
        connect(m_doc, &Document::canvasSizeChanged, this, [this] { invalidateTiles(QRect()); });
        connect(m_doc, &Document::selectionChanged, this, [this] { update(); });
    }
    zoomToFit();
    update();
}

void CanvasView::setToolManager(ToolManager* tm)
{
    m_tools = tm;
    if (tm) {
        connect(tm, &ToolManager::activeToolChanged, this, [this](Tool* t) {
            setActiveTool(t);
            update();
        });
        setActiveTool(tm->active());
    }
}

Tool* CanvasView::activeTool() const
{
    return m_tool;
}

void CanvasView::setActiveTool(Tool* t)
{
    if (m_tool == t)
        return;
    if (m_tool) {
        m_tool->disconnect(this);
        m_tool->deactivate();
    }
    m_tool = t;
    if (m_tool) {
        m_tool->setView(this);
        m_tool->setDocument(m_doc);
        connect(m_tool, &Tool::updateRequested, this, [this](const QRect& r) { invalidateTiles(r); });
        connect(m_tool, &Tool::overlayUpdateRequested, this, qOverload<>(&QWidget::update));
        m_tool->activate();
    }
    update();
}

// ------------------------------------------------------------------ tiles

void CanvasView::invalidateTiles(const QRect& docRect)
{
    if (!m_doc)
        return;
    if (docRect.isNull()) {
        m_dirtyRegion = QRect();
        m_compositeCache = QImage();
        m_cacheRect = QRect();
    } else {
        m_dirtyRegion = m_dirtyRegion.isNull() ? docRect : m_dirtyRegion.united(docRect);
    }
    update();
}

void CanvasView::onImageChanged(int, const QRect&)
{
    invalidateTiles(QRect());
}

void CanvasView::onDocumentChanged()
{
    invalidateTiles(QRect());
}

QImage CanvasView::compositeImage()
{
    if (!m_doc)
        return QImage();
    const QSize sz = m_doc->size();
    if (m_compositeCache.size() != sz || m_cacheRect != QRect(QPoint(0, 0), sz)) {
        m_compositeCache = m_doc->compositeImage();
        m_cacheRect = QRect(QPoint(0, 0), sz);
        m_dirtyRegion = QRect();
    } else if (!m_dirtyRegion.isNull()) {
        const QRect r = m_dirtyRegion.intersected(QRect(QPoint(0, 0), sz));
        if (!r.isEmpty()) {
            Surface tmp;
            Renderer::compositeRegion(m_doc->layers(), QRect(QPoint(0, 0), sz), tmp, r.topLeft());
            QPainter p(&m_compositeCache);
            p.setCompositionMode(QPainter::CompositionMode_Source);
            p.drawImage(r.topLeft(), tmp.toQImage());
            p.end();
        }
        m_dirtyRegion = QRect();
    }
    return m_compositeCache;
}

// ------------------------------------------------------------------ geometry

QPointF CanvasView::imageToWidget(const QPointF& p) const
{
    return QPointF(m_scroll.x() + p.x() * m_zoom, m_scroll.y() + p.y() * m_zoom);
}

QPointF CanvasView::widgetToImageF(const QPointF& p) const
{
    if (m_zoom <= 0)
        return QPointF(0, 0);
    return QPointF((p.x() - m_scroll.x()) / m_zoom, (p.y() - m_scroll.y()) / m_zoom);
}

QPoint CanvasView::widgetToImage(const QPoint& p) const
{
    const QPointF f = widgetToImageF(QPointF(p));
    return QPoint(int(std::floor(f.x() + 0.5)), int(std::floor(f.y() + 0.5)));
}

QRect CanvasView::imageRectToWidget(const QRect& r) const
{
    return QRect(int(std::floor(imageToWidget(QPointF(r.topLeft())).x() - 0.5)),
                 int(std::floor(imageToWidget(QPointF(r.topLeft())).y() - 0.5)),
                 qMax(1, int(std::ceil(r.width() * m_zoom))), qMax(1, int(std::ceil(r.height() * m_zoom))));
}

QRectF CanvasView::viewRect() const
{
    return QRectF(rulerRect());
}

void CanvasView::setZoom(double z, const QPointF& anchorInWidget)
{
    if (!m_doc)
        return;
    z = qBound(0.01, z, 32.0);
    if (qFuzzyCompare(z, m_zoom))
        return;
    m_userZoomed = true;
    const QPointF imgAnchor = widgetToImageF(anchorInWidget);
    m_zoom = z;
    m_scroll = QPointF(anchorInWidget.x() - imgAnchor.x() * m_zoom, anchorInWidget.y() - imgAnchor.y() * m_zoom);
    emit zoomChanged(m_zoom);
    update();
}

void CanvasView::zoomIn()
{
    const int i = indexForZoom(m_zoom);
    setZoom(zoomForIndex(i + 1), QPointF(width() / 2.0, height() / 2.0));
}

void CanvasView::zoomOut()
{
    const int i = indexForZoom(m_zoom);
    setZoom(zoomForIndex(i - 1), QPointF(width() / 2.0, height() / 2.0));
}

void CanvasView::zoomToFit()
{
    if (!m_doc)
        return;
    const QRect avail = rulerRect();
    // Before the layout has run the view has no usable size yet; doing the
    // maths anyway would clamp the zoom to the minimum and leave the image
    // as a few pixels in the corner.
    if (avail.width() < 8 || avail.height() < 8)
        return;
    const double z = qMin(double(avail.width()) / m_doc->width(), double(avail.height()) / m_doc->height());
    m_zoom = qBound(0.01, z, 32.0);
    m_scroll = QPointF(avail.left() + (avail.width() - m_doc->width() * m_zoom) / 2.0,
                       avail.top() + (avail.height() - m_doc->height() * m_zoom) / 2.0);
    m_userZoomed = false; // an explicit "fit" keeps tracking later resizes
    emit zoomChanged(m_zoom);
    update();
}

void CanvasView::zoomToSelection()
{
    if (!m_doc || !m_doc->hasSelection())
        return;
    const QRect r = m_doc->selectionBounds();
    if (r.isEmpty())
        return;
    const QRect avail = rulerRect();
    const double z = qMin(double(avail.width()) / r.width(), double(avail.height()) / r.height());
    m_zoom = qBound(0.01, z, 32.0);
    m_scroll = QPointF(avail.left() + (avail.width() - r.width() * m_zoom) / 2.0 - r.left() * m_zoom,
                       avail.top() + (avail.height() - r.height() * m_zoom) / 2.0 - r.top() * m_zoom);
    emit zoomChanged(m_zoom);
    update();
}

void CanvasView::zoomOriginal()
{
    setZoom(1.0, QPointF(width() / 2.0, height() / 2.0));
}

void CanvasView::setZoomPercent(int percent)
{
    setZoom(qBound(1, percent, 3200) / 100.0, QPointF(width() / 2.0, height() / 2.0));
}

void CanvasView::scrollTo(const QPointF& widgetPoint, const QPointF& imagePoint)
{
    m_scroll = QPointF(widgetPoint.x() - imagePoint.x() * m_zoom, widgetPoint.y() - imagePoint.y() * m_zoom);
    update();
}

QRect CanvasView::rulerRect() const
{
    const int r = m_rulers ? RulerSize : 0;
    return QRect(r, r, width() - r, height() - r);
}

void CanvasView::setRulersVisible(bool v)
{
    if (m_rulers == v)
        return;
    m_rulers = v;
    update();
}

QPointF CanvasView::snapPoint(const QPointF& p) const
{
    if (!m_snap)
        return p;
    QPointF out = p;
    const double threshold = 6.0 / m_zoom;
    for (int gy : std::as_const(m_hGuides)) {
        if (qAbs(p.y() - gy) <= threshold) {
            out.setY(gy);
            break;
        }
    }
    for (int gx : std::as_const(m_vGuides)) {
        if (qAbs(p.x() - gx) <= threshold) {
            out.setX(gx);
            break;
        }
    }
    if (gridSnapEnabled() && m_gridSize > 0) {
        const int g = m_gridSize;
        if (qAbs(out.x() / g - std::round(out.x() / g)) * g < threshold)
            out.setX(std::round(out.x() / g) * g);
        if (qAbs(out.y() / g - std::round(out.y() / g)) * g < threshold)
            out.setY(std::round(out.y() / g) * g);
    }
    return out;
}

void CanvasView::addHorizontalGuide(int y)
{
    if (!m_hGuides.contains(y))
        m_hGuides.append(y);
    m_guides = true;
    update();
}

void CanvasView::addVerticalGuide(int x)
{
    if (!m_vGuides.contains(x))
        m_vGuides.append(x);
    m_guides = true;
    update();
}

void CanvasView::removeHorizontalGuide(int y) { m_hGuides.removeAll(y); update(); }
void CanvasView::removeVerticalGuide(int x) { m_vGuides.removeAll(x); update(); }
void CanvasView::clearGuides() { m_hGuides.clear(); m_vGuides.clear(); update(); }

void CanvasView::setCursorImagePos(const QPoint& p) { m_lastCursor = p; }

void CanvasView::onAntsTimer()
{
    if (m_doc && m_doc->hasSelection()) {
        m_antsOffset = (m_antsOffset + 1) % 8;
        update();
    }
}

void CanvasView::setAnimationsEnabled(bool on)
{
    // Repainting while the window manager is dragging the window makes the drag
    // stutter, so the animation is suspended for the duration of a move.
    if (on == m_animationsEnabled)
        return;
    m_animationsEnabled = on;
    if (on) {
        if (m_doc && m_doc->hasSelection() && !m_antsTimer->isActive())
            m_antsTimer->start();
    } else {
        m_antsTimer->stop();
    }
    update();
}
// ------------------------------------------------------------------ painting

void CanvasView::drawCheckerboard(QPainter& p, const QRectF& r) const
{
    const int step = 8;
    p.save();
    p.setClipRect(r);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(204, 204, 204));
    p.translate(m_scroll);
    p.scale(m_zoom, m_zoom);
    const QRectF ir = QRectF(0, 0, m_doc ? m_doc->width() : 0, m_doc ? m_doc->height() : 0);
    for (int y = int(std::floor(ir.top() / step)) * step; y < ir.bottom(); y += step) {
        for (int x = int(std::floor(ir.left() / step)) * step; x < ir.right(); x += step) {
            if ((((x / step) + (y / step)) & 1) == 0)
                p.drawRect(QRectF(x, y, step, step));
        }
    }
    p.restore();
}

void CanvasView::drawSelectionAnts(QPainter& p) const
{
    if (!m_doc || m_doc->selection().isNull())
        return;
    const Selection& sel = m_doc->selection();
    const QImage& mask = sel.mask();
    if (mask.isNull())
        return;
    const double inv = 1.0 / m_zoom;
    p.save();
    p.setPen(QPen(QColor(0, 0, 0), inv));
    p.setBrush(Qt::NoBrush);
    // Walk the mask and draw the boundary segments between selected/unselected.
    for (int y = 0; y < mask.height(); ++y) {
        const uchar* row = mask.constScanLine(y);
        for (int x = 0; x < mask.width(); ++x) {
            if (row[x] < 128)
                continue;
            const bool up = y > 0 ? mask.constScanLine(y - 1)[x] >= 128 : false;
            const bool down = y + 1 < mask.height() ? mask.constScanLine(y + 1)[x] >= 128 : false;
            const bool left = x > 0 ? row[x - 1] >= 128 : false;
            const bool right = x + 1 < mask.width() ? row[x + 1] >= 128 : false;
            if (!up)
                p.drawLine(QPointF(x, y), QPointF(x + 1, y));
            if (!down)
                p.drawLine(QPointF(x, y + 1), QPointF(x + 1, y + 1));
            if (!left)
                p.drawLine(QPointF(x, y), QPointF(x, y + 1));
            if (!right)
                p.drawLine(QPointF(x + 1, y), QPointF(x + 1, y + 1));
        }
    }
    p.restore();
    p.save();
    QPen dashPen(QColor(255, 255, 255), inv, Qt::SolidLine);
    dashPen.setDashPattern({ 4.0 * inv, 4.0 * inv });
    dashPen.setDashOffset(-m_antsOffset * inv);
    p.setPen(dashPen);
    for (int y = 0; y < mask.height(); ++y) {
        const uchar* row = mask.constScanLine(y);
        for (int x = 0; x < mask.width(); ++x) {
            if (row[x] < 128)
                continue;
            const bool up = y > 0 ? mask.constScanLine(y - 1)[x] >= 128 : false;
            const bool down = y + 1 < mask.height() ? mask.constScanLine(y + 1)[x] >= 128 : false;
            const bool left = x > 0 ? row[x - 1] >= 128 : false;
            const bool right = x + 1 < mask.width() ? row[x + 1] >= 128 : false;
            if (!up)
                p.drawLine(QPointF(x, y), QPointF(x + 1, y));
            if (!down)
                p.drawLine(QPointF(x, y + 1), QPointF(x + 1, y + 1));
            if (!left)
                p.drawLine(QPointF(x, y), QPointF(x, y + 1));
            if (!right)
                p.drawLine(QPointF(x + 1, y), QPointF(x + 1, y + 1));
        }
    }
    p.restore();
}

void CanvasView::drawGuides(QPainter& p) const
{
    if (!m_guides || !m_doc)
        return;
    const double inv = 1.0 / m_zoom;
    p.save();
    p.setPen(QPen(QColor(0, 180, 255, 200), inv, Qt::DashLine));
    p.setBrush(Qt::NoBrush);
    for (int gy : m_hGuides) {
        const QPointF a = imageToWidget(QPointF(0, gy));
        p.drawLine(QPointF(rulerRect().left(), a.y()), QPointF(width(), a.y()));
    }
    for (int gx : m_vGuides) {
        const QPointF a = imageToWidget(QPointF(gx, 0));
        p.drawLine(QPointF(a.x(), rulerRect().top()), QPointF(a.x(), height()));
    }
    p.restore();
}

void CanvasView::drawOverlay(QPainter& p) const
{
    if (m_tool) {
        p.save();
        p.translate(m_scroll);
        p.scale(m_zoom, m_zoom);
        m_tool->drawOverlay(p);
        p.restore();
    }
}

void CanvasView::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::SmoothPixmapTransform, m_zoom < 1.0);
    p.fillRect(rect(), QColor(128, 128, 128));

    if (!m_doc) {
        if (m_rulers)
            p.fillRect(0, 0, width(), height(), QColor(240, 240, 240));
        return;
    }

    const QRect canvasRect = rulerRect();
    p.setClipRect(canvasRect);
    drawCheckerboard(p, canvasRect);

    const QImage img = compositeImage();
    if (!img.isNull()) {
        const QRectF target(imageToWidget(QPointF(0, 0)),
                            QSizeF(m_doc->width() * m_zoom, m_doc->height() * m_zoom));
        p.drawImage(target, img, QRectF(0, 0, img.width(), img.height()));
        // Pixel grid at high zoom levels.
        if (m_zoom >= 8.0) {
            p.setPen(QPen(QColor(0, 0, 0, 40), 1.0));
            const QRect vis = canvasRect.intersected(QRect(target.toRect()));
            for (int x = vis.left(); x <= vis.right(); ++x) {
                const int ix = widgetToImage(QPoint(x, canvasRect.center().y())).x();
                const double wx = imageToWidget(QPointF(ix, 0)).x();
                p.drawLine(QPointF(wx, vis.top()), QPointF(wx, vis.bottom()));
            }
            for (int y = vis.top(); y <= vis.bottom(); ++y) {
                const int iy = widgetToImage(QPoint(canvasRect.center().x(), y)).y();
                const double wy = imageToWidget(QPointF(0, iy)).y();
                p.drawLine(QPointF(vis.left(), wy), QPointF(vis.right(), wy));
            }
        }
    }

    if (m_grid && m_gridSize > 0) {
        p.save();
        p.setPen(QPen(m_gridColor, 1.0));
        const QRect vis = canvasRect.intersected(imageRectToWidget(m_doc->bounds()));
        for (int gx = 0; gx <= m_doc->width(); gx += m_gridSize) {
            const double wx = imageToWidget(QPointF(gx, 0)).x();
            p.drawLine(QPointF(wx, vis.top()), QPointF(wx, vis.bottom()));
        }
        for (int gy = 0; gy <= m_doc->height(); gy += m_gridSize) {
            const double wy = imageToWidget(QPointF(0, gy)).y();
            p.drawLine(QPointF(vis.left(), wy), QPointF(vis.right(), wy));
        }
        p.restore();
    }

    drawGuides(p);
    drawSelectionAnts(p);
    drawOverlay(p);
    p.setClipping(false);

    if (m_rulers) {
        p.save();
        const QColor bg(240, 240, 240);
        const QColor fg(60, 60, 60);
        p.fillRect(0, 0, width(), RulerSize, bg);
        p.fillRect(0, 0, RulerSize, height(), bg);
        p.setPen(QPen(QColor(120, 120, 120), 1));
        p.drawLine(0, RulerSize, width(), RulerSize);
        p.drawLine(RulerSize, 0, RulerSize, height());

        // Pick a 1/2/5 x 10^n step so ticks land on round numbers and stay at
        // least 50 px apart on screen.
        const double minPx = 50.0;
        int magnitude = 1;
        while (magnitude * m_zoom < minPx)
            magnitude *= 10;
        int imgStep = magnitude;
        for (int mult : { 1, 2, 5, 10 }) {
            imgStep = magnitude * mult;
            if (imgStep * m_zoom >= minPx)
                break;
        }

        p.setPen(fg);
        p.setFont(QFont(p.font().family(), 7));
        // Top ruler: walks the image width, labels drawn horizontally.
        for (int x = 0; x <= m_doc->width(); x += imgStep) {
            const double wx = imageToWidget(QPointF(x, 0)).x();
            if (wx < RulerSize)
                continue;
            p.drawLine(QPoint(int(wx), RulerSize - 5), QPoint(int(wx), RulerSize));
            p.drawText(QRect(int(wx) + 2, 1, 44, RulerSize - 7), Qt::AlignLeft | Qt::AlignTop,
                       QString::number(x));
        }
        // Left ruler: walks the image height, labels rotated and read upwards.
        for (int y = 0; y <= m_doc->height(); y += imgStep) {
            const double wy = imageToWidget(QPointF(0, y)).y();
            if (wy < RulerSize)
                continue;
            p.drawLine(QPoint(RulerSize - 5, int(wy)), QPoint(RulerSize, int(wy)));
            p.save();
            p.translate(RulerSize - 3, int(wy) - 2);
            p.rotate(-90);
            p.drawText(QRect(0, -12, 40, 11), Qt::AlignLeft | Qt::AlignVCenter, QString::number(y));
            p.restore();
        }
        // Cursor markers.
        if (m_lastCursor.x() >= 0) {
            const double wy = imageToWidget(QPointF(0, m_lastCursor.y())).y();
            const double wx = imageToWidget(QPointF(m_lastCursor.x(), 0)).x();
            p.setPen(QPen(QColor(200, 40, 40), 1));
            p.drawLine(QPoint(RulerSize, int(wy)), QPoint(int(wx), int(wy)));
            p.drawLine(QPoint(int(wx), RulerSize), QPoint(int(wx), int(m_doc->height() * m_zoom + m_scroll.y())));
        }
        p.restore();
    }
}

void CanvasView::updateRulers(const QRect&)
{
    update();
}

void CanvasView::resizeEvent(QResizeEvent* e)
{
    QWidget::resizeEvent(e);
    emit canvasResized();
    // Keep "fit on screen" alive while the user has not zoomed manually, so the
    // image is correct no matter when the layout finally settles.
    if (!m_userZoomed && m_doc)
        zoomToFit();
    else
        update();
}

void CanvasView::enterEvent(QEnterEvent* e)
{
    QWidget::enterEvent(e);
    updateCursorPos(e->position().toPoint());
}

void CanvasView::leaveEvent(QEvent* e)
{
    QWidget::leaveEvent(e);
    m_lastCursor = QPoint(-1, -1);
    emit cursorMoved(m_lastCursor);
    update();
}

void CanvasView::updateCursorPos(const QPoint& widgetPos)
{
    const QPoint img = widgetToImage(widgetPos);
    if (img == m_lastCursor)
        return;
    m_lastCursor = img;
    emit cursorMoved(img);
    if (m_tool) {
        // A stroke is defined by the button and modifiers it started with, so
        // those - not "no button" - have to reach the tool while dragging.
        m_tool->mouseDrag(snapPoint(widgetToImageF(QPointF(widgetPos))), m_dragButtons, m_dragMods);
    }
    update();
}

void CanvasView::mousePressEvent(QMouseEvent* e)
{
    setFocus(Qt::MouseFocusReason);
    const QPoint img = widgetToImage(e->pos());
    if (m_spaceDown || e->button() == Qt::MiddleButton) {
        m_panning = true;
        m_panButton = e->button();
        m_panAnchor = e->pos();
        setCursor(Qt::ClosedHandCursor);
        return;
    }
    if (!m_doc)
        return;
    if (m_rulers && (e->pos().x() < RulerSize || e->pos().y() < RulerSize)) {
        if (e->button() == Qt::LeftButton) {
            const int val = e->pos().y() < RulerSize ? img.x() : img.y();
            if (e->pos().y() < RulerSize)
                addVerticalGuide(val);
            else
                addHorizontalGuide(val);
        }
        return;
    }
    if (m_tool) {
        const QPointF snapped = snapPoint(widgetToImageF(QPointF(e->pos())));
        m_dragButtons = e->button();
        m_dragMods = e->modifiers();
        m_tool->mouseDown(QPoint(int(std::floor(snapped.x() + 0.5)), int(std::floor(snapped.y() + 0.5))),
                          e->button(), e->modifiers());
    }
}

void CanvasView::mouseMoveEvent(QMouseEvent* e)
{
    if (m_panning) {
        const QPoint delta = e->pos() - m_panAnchor;
        m_scroll += QPointF(delta);
        m_panAnchor = e->pos();
        update();
        return;
    }
    updateCursorPos(e->pos());
}

void CanvasView::mouseReleaseEvent(QMouseEvent* e)
{
    if (m_panning) {
        m_panning = false;
        m_panButton = Qt::NoButton;
        unsetCursor();
        return;
    }
    if (!m_tool || !m_doc)
        return;
    if (m_rulers && (e->pos().x() < RulerSize || e->pos().y() < RulerSize))
        return;
    const QPointF snapped = snapPoint(widgetToImageF(QPointF(e->pos())));
    m_tool->mouseUp(QPoint(int(std::floor(snapped.x() + 0.5)), int(std::floor(snapped.y() + 0.5))),
                    e->button(), e->modifiers());
    m_dragButtons = Qt::NoButton;
    m_dragMods = Qt::NoModifier;
}

void CanvasView::mouseDoubleClickEvent(QMouseEvent* e)
{
    if (!m_tool || !m_doc)
        return;
    m_tool->mouseDoubleClick(widgetToImage(e->pos()), e->button(), e->modifiers());
}

void CanvasView::wheelEvent(QWheelEvent* e)
{
    const QPointF anchor = e->position();
    if (e->modifiers() & Qt::ControlModifier) {
        const double delta = e->angleDelta().y() / 120.0;
        const int i = indexForZoom(m_zoom) + (delta > 0 ? 1 : (delta < 0 ? -1 : 0));
        setZoom(zoomForIndex(i), anchor);
    } else {
        // Scroll vertically / horizontally.
        const QPoint d = e->angleDelta();
        m_scroll += QPointF(-d.x() * 0.5, -d.y() * 0.5);
        update();
    }
    e->accept();
}

void CanvasView::keyPressEvent(QKeyEvent* e)
{
    if (e->key() == Qt::Key_Space && !m_spaceDown) {
        m_spaceDown = true;
        setCursor(Qt::OpenHandCursor);
        e->accept();
        return;
    }
    if (m_tool) {
        m_tool->keyPressEvent(e);
        if (e->isAccepted())
            return;
    }
    QWidget::keyPressEvent(e);
}

void CanvasView::keyReleaseEvent(QKeyEvent* e)
{
    if (e->key() == Qt::Key_Space) {
        m_spaceDown = false;
        if (!m_panning)
            unsetCursor();
        e->accept();
        return;
    }
    if (m_tool) {
        m_tool->keyReleaseEvent(e);
        if (e->isAccepted())
            return;
    }
    QWidget::keyReleaseEvent(e);
}

} // namespace pnq
