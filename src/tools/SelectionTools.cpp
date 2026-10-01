// Selection tools: rectangular, elliptical, lasso, magic wand, opaque/
// transparent and "Move Selected".
//
// All of them only produce a selection on mouse-up (the drag shows a dashed
// preview through drawOverlay), which is exactly how Paint.NET behaves.

#include "tools/Tool.h"

#include "core/Document.h"
#include "core/History.h"
#include "core/Renderer.h"
#include "core/Selection.h"
#include "core/Surface.h"
#include "tools/ToolManager.h"
#include "tools/ToolSupport.h"
#include "ui/CanvasView.h"
#include "ui/ToolOptions.h"

#include <QCheckBox>
#include <QComboBox>
#include <QKeyEvent>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QPolygonF>
#include <QSlider>
#include <QSpinBox>
#include <QBoxLayout>

namespace pnq {
namespace {

// ------------------------------------------------------------------ helpers

Selection deepCopySelection(const Selection& s)
{
    Selection out;
    if (s.isNull())
        return out;
    out.setMask(s.mask().copy());
    return out;
}

/// The selection the new shape is combined with (an empty, non-null mask when
/// the document currently has no selection).
Selection combinationBase(const Document* doc)
{
    const Selection& cur = doc->selection();
    if (cur.isNull() || cur.isEmpty())
        return Selection(doc->width(), doc->height(), false);
    return cur;
}

/// Applies `shape` to the document selection honouring New/Add/Subtract/
/// Intersect and records a single undoable step.
bool commitSelection(Document* doc, const Selection& shape, int mode, const QString& name)
{
    if (!doc)
        return false;
    const Selection before = deepCopySelection(doc->selection());
    Selection after;
    switch (mode) {
    case 1:
        after = combinationBase(doc);
        after += shape;
        break;
    case 2:
        after = combinationBase(doc);
        after -= shape;
        break;
    case 3:
        after = combinationBase(doc);
        after &= shape;
        break;
    default:
        after = shape;
        break;
    }
    if (after.isEmpty())
        after = Selection();  // nothing left -> no selection at all
    if (after == before)
        return false;
    doc->setSelection(after);
    if (doc->history())
        doc->history()->push(new SelectionAction(name, before, deepCopySelection(after)));
    return true;
}

/// Path of an inclusive pixel rectangle.
QPainterPath rectPath(const QRect& r)
{
    QPainterPath path;
    path.addRect(QRectF(r));
    return path;
}

/// Rasterises a path into a mask restricted to `area` (mask coordinates are
/// relative to `area`). Passing the document bounds produces a full canvas
/// selection, which is what the rest of the core expects. (Selection's own
/// selectRect()/selectPath() helpers resize their mask to the shape's bounding
/// box, so the mask has to be built here.)
Selection maskFromPath(const QRect& area, const QPainterPath& path)
{
    Selection out;
    if (area.isEmpty())
        return out;
    QImage m(area.size(), QImage::Format_Alpha8);
    m.fill(0);
    {
        QPainter p(&m);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setPen(Qt::NoPen);
        p.setBrush(Qt::white);
        p.translate(-area.topLeft());
        p.drawPath(path);
    }
    out.setMask(m);
    return out;
}

/// Dashed, screen-constant preview rectangle.
void drawDashedRect(QPainter& p, const QRectF& r, const QColor& color)
{
    QPen pen(color);
    pen.setCosmetic(true);
    pen.setStyle(Qt::DashLine);
    pen.setDashPattern({ 4.0, 3.0 });
    p.save();
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    p.drawRect(r);
    p.restore();
}

void drawDashedLine(QPainter& p, const QPointF& a, const QPointF& b, const QColor& color)
{
    QPen pen(color);
    pen.setCosmetic(true);
    pen.setStyle(Qt::DashLine);
    pen.setDashPattern({ 4.0, 3.0 });
    p.save();
    p.setPen(pen);
    p.drawLine(a, b);
    p.restore();
}

/// Marching ants for a mask expressed in `area` coordinates.
void drawLocalAnts(QPainter& p, const Selection& localSel, const QRect& area, double zoom)
{
    if (localSel.isNull())
        return;
    p.save();
    p.translate(area.topLeft());
    if (zoom > 0.0)
        p.scale(1.0 / zoom, 1.0 / zoom);
    drawSelectionOutline(p, localSel, QColor(255, 255, 255), QColor(0, 0, 0), 0.0);
    p.restore();
}

/// Pixel copy with an optional mask and source-over blending.
void blitRegion(Surface& dst, const QPoint& dstPos, const QImage& src, bool blend, const QImage* mask)
{
    if (src.isNull())
        return;
    const QRect clip = QRect(dstPos, src.size()).intersected(dst.bounds());
    for (int y = clip.top(); y <= clip.bottom(); ++y) {
        const int sy = y - dstPos.y();
        pixel_t* d = dst.scanLine(y);
        const pixel_t* s = reinterpret_cast<const pixel_t*>(src.constScanLine(sy));
        const uchar* m = mask ? mask->constScanLine(sy) : nullptr;
        for (int x = clip.left(); x <= clip.right(); ++x) {
            const int sx = x - dstPos.x();
            if (!blend) {
                d[x] = s[sx];
                continue;
            }
            pixel_t sp = s[sx];
            if (sp == 0)
                continue;
            if (m) {
                const int v = m[sx];
                if (v == 0)
                    continue;
                if (v != 255)
                    sp = qPremult(quint8(getA(sp) * v / 255), quint8(getR(sp) * v / 255),
                                  quint8(getG(sp) * v / 255), quint8(getB(sp) * v / 255));
            }
            d[x] = composePixel(d[x], sp, BlendMode::Normal);
        }
    }
}

/// Restores `src` over `area` of the layer and reports the change.
void restoreRegion(Tool* tool, Document* doc, int layerIndex, const QRect& area, const Surface& src)
{
    Layer* l = doc->layerAt(layerIndex);
    if (!l)
        return;
    blitRegion(l->surface(), area.topLeft(), src.toQImageConst(), false, nullptr);
    doc->notifyLayerPixels(layerIndex, area);
    if (tool)
        tool->requestUpdate(area);
}

double viewZoom(Tool* t) { return t && t->view() ? t->view()->zoom() : 1.0; }

/// Multiplies a premultiplied pixel by a 0..255 factor.
pixel_t scalePixel(pixel_t p, int factor)
{
    if (factor <= 0)
        return 0;
    if (factor >= 255)
        return p;
    return qPremult(quint8(getA(p) * factor / 255), quint8(getR(p) * factor / 255),
                    quint8(getG(p) * factor / 255), quint8(getB(p) * factor / 255));
}

/// Commits a pixel change as exactly one history step, wrapped in a macro so a
/// shape is undone as a single unit rather than stroke by stroke.
void pushShapeAction(Tool* tool, const QString& name, int layerIndex, const QRect& rect,
                     const Surface& before, const Surface& after)
{
    if (!tool || !tool->document() || rect.isEmpty())
        return;
    Document* doc = tool->document();
    if (!doc->history())
        return;
    MacroAction* macro = new MacroAction(name);
    macro->add(new PixelDeltaAction(name, layerIndex, rect, before, after));
    doc->history()->push(macro);
}

// ---------------------------------------------------------- selection base

/// Shared drag/option handling for the tools that build a selection by dragging.
class SelectionToolBase : public Tool
{
public:
    explicit SelectionToolBase(QObject* parent) : Tool(parent) {}

    int mode() const { return m_mode; }

protected:
    void addModeRow(QBoxLayout* layout)
    {
        m_modeBox = qobject_cast<QComboBox*>(ToolOptions::addCombo(
            layout, tr("Mode"),
            { tr("New"), tr("Add"), tr("Subtract"), tr("Intersect") }, m_mode));
        connect(m_modeBox, &QComboBox::currentIndexChanged, this, [this](int i) {
            m_mode = i;
            refreshStatus();
        });
    }

    void beginDrag(const QPoint& p, Qt::MouseButton b)
    {
        m_press = p;
        m_cur = p;
        m_dragging = true;
        m_button = b;
    }

    void endDrag()
    {
        m_dragging = false;
        requestOverlayUpdate();
    }

    /// Right dragging always subtracts, like in Paint.NET.
    int effectiveMode() const { return m_button == Qt::RightButton ? 2 : m_mode; }

    /// Inclusive pixel rectangle between two drag points.
    static QRect dragRect(const QPoint& a, const QPoint& b) { return QRect(a, b).normalized(); }
    static QRectF dragRectF(const QPoint& a, const QPoint& b)
    {
        return QRectF(QPointF(a), QPointF(b)).normalized().adjusted(0, 0, 1, 1);
    }

    QColor primaryPreviewColor() const
    {
        QColor c = toQColor(colorForButton(Qt::LeftButton));
        c.setAlpha(190);
        return c;
    }

    int m_mode = 0;
    QComboBox* m_modeBox = nullptr;
    QPoint m_press;
    QPoint m_cur;
    bool m_dragging = false;
    Qt::MouseButton m_button = Qt::LeftButton;
};

// ------------------------------------------------------------- select rect

class RectangularSelectionTool : public SelectionToolBase
{
public:
    explicit RectangularSelectionTool(QObject* parent) : SelectionToolBase(parent) {}

    QString id() const override { return QStringLiteral("selectrect"); }
    QString name() const override { return tr("Rectangular Selection"); }
    QString toolTip() const override { return tr("Select a rectangular area of the image"); }
    QString shortcutString() const override { return QStringLiteral("M"); }
    int sortOrder() const override { return 100; }
    Qt::CursorShape cursorShape() const override { return Qt::CrossCursor; }

    QString statusText() const override
    {
        if (!m_dragging)
            return QString();
        const QRect r = dragRect(m_press, m_cur);
        return tr("%1 x %2 at %3, %4").arg(r.width()).arg(r.height()).arg(r.left()).arg(r.top());
    }

    QWidget* createOptionsWidget(QWidget* parent) override
    {
        QBoxLayout* layout = nullptr;
        QWidget* root = ToolOptions::createRoot(parent, &layout);
        addModeRow(layout);
        m_optionsWidget = root;
        return root;
    }

    void mouseDown(const QPoint& docPos, Qt::MouseButton button, Qt::KeyboardModifiers) override
    {
        if (!document())
            return;
        beginDrag(docPos, button);
        requestOverlayUpdate();
    }

    void mouseMove(const QPoint& docPos, Qt::MouseButtons, Qt::KeyboardModifiers) override
    {
        if (!m_dragging)
            return;
        m_cur = docPos;
        requestOverlayUpdate();
        refreshStatus();
    }

    void mouseUp(const QPoint& docPos, Qt::MouseButton, Qt::KeyboardModifiers) override
    {
        if (!m_dragging)
            return;
        m_cur = docPos;
        const QRect r = dragRect(m_press, m_cur).intersected(document()->bounds());
        const int mode = effectiveMode();
        endDrag();
        if (r.isEmpty())
            return;
        Selection s = maskFromPath(document()->bounds(), rectPath(r));
        if (commitSelection(document(), s, mode, name()))
            requestUpdate(document()->bounds());
    }

    void drawOverlay(QPainter& p) override
    {
        if (!m_dragging)
            return;
        drawDashedRect(p, dragRectF(m_press, m_cur), primaryPreviewColor());
    }
};

// ----------------------------------------------------------- select ellipse

class EllipticalSelectionTool : public SelectionToolBase
{
public:
    explicit EllipticalSelectionTool(QObject* parent) : SelectionToolBase(parent) {}

    QString id() const override { return QStringLiteral("selectellipse"); }
    QString name() const override { return tr("Elliptical Selection"); }
    QString toolTip() const override { return tr("Select an elliptical area of the image"); }
    QString shortcutString() const override { return QStringLiteral("Shift+M"); }
    int sortOrder() const override { return 101; }

    QString statusText() const override
    {
        if (!m_dragging)
            return QString();
        const QRect r = dragRect(m_press, m_cur);
        return tr("%1 x %2 at %3, %4").arg(r.width()).arg(r.height()).arg(r.left()).arg(r.top());
    }

    QWidget* createOptionsWidget(QWidget* parent) override
    {
        QBoxLayout* layout = nullptr;
        QWidget* root = ToolOptions::createRoot(parent, &layout);
        addModeRow(layout);
        m_optionsWidget = root;
        return root;
    }

    void mouseDown(const QPoint& docPos, Qt::MouseButton button, Qt::KeyboardModifiers) override
    {
        if (!document())
            return;
        beginDrag(docPos, button);
        requestOverlayUpdate();
    }

    void mouseMove(const QPoint& docPos, Qt::MouseButtons, Qt::KeyboardModifiers) override
    {
        if (!m_dragging)
            return;
        m_cur = docPos;
        requestOverlayUpdate();
        refreshStatus();
    }

    void mouseUp(const QPoint& docPos, Qt::MouseButton, Qt::KeyboardModifiers) override
    {
        if (!m_dragging)
            return;
        m_cur = docPos;
        const QRect r = dragRect(m_press, m_cur).intersected(document()->bounds());
        const int mode = effectiveMode();
        endDrag();
        if (r.isEmpty())
            return;
        QPainterPath path;
        path.addEllipse(QRectF(r));
        Selection s = maskFromPath(document()->bounds(), path);
        if (commitSelection(document(), s, mode, name()))
            requestUpdate(document()->bounds());
    }

    void drawOverlay(QPainter& p) override
    {
        if (!m_dragging)
            return;
        const QRectF r = dragRectF(m_press, m_cur);
        p.save();
        p.setPen(Qt::NoPen);
        p.setBrush(primaryPreviewColor());
        p.drawEllipse(r);
        p.restore();
        drawDashedRect(p, r, QColor(255, 255, 255, 220));
    }
};

// ---------------------------------------------------------------- lasso

class FreeformSelectionTool : public SelectionToolBase
{
public:
    explicit FreeformSelectionTool(QObject* parent) : SelectionToolBase(parent) {}

    QString id() const override { return QStringLiteral("lasso"); }
    QString name() const override { return tr("Freeform Selection"); }
    QString toolTip() const override { return tr("Drag a free-hand outline to select an area"); }
    QString shortcutString() const override { return QStringLiteral("A"); }
    int sortOrder() const override { return 102; }

    QString statusText() const override
    {
        if (!m_dragging)
            return QString();
        return tr("%1 points").arg(m_points.size());
    }

    QWidget* createOptionsWidget(QWidget* parent) override
    {
        QBoxLayout* layout = nullptr;
        QWidget* root = ToolOptions::createRoot(parent, &layout);
        addModeRow(layout);
        m_optionsWidget = root;
        return root;
    }

    void activate() override
    {
        Tool::activate();
        m_points.clear();
    }

    void deactivate() override
    {
        m_points.clear();
        Tool::deactivate();
    }

    void mouseDown(const QPoint& docPos, Qt::MouseButton button, Qt::KeyboardModifiers) override
    {
        if (!document())
            return;
        beginDrag(docPos, button);
        m_points.clear();
        m_points.append(docPos);
        requestOverlayUpdate();
    }

    void mouseMove(const QPoint& docPos, Qt::MouseButtons, Qt::KeyboardModifiers) override
    {
        if (!m_dragging)
            return;
        m_cur = docPos;
        if (m_points.isEmpty() || (docPos - m_points.last()).manhattanLength() >= 2)
            m_points.append(docPos);
        requestOverlayUpdate();
        refreshStatus();
    }

    void mouseUp(const QPoint& docPos, Qt::MouseButton, Qt::KeyboardModifiers) override
    {
        if (!m_dragging)
            return;
        if ((docPos - m_points.last()).manhattanLength() >= 1)
            m_points.append(docPos);
        m_cur = docPos;
        const int mode = effectiveMode();
        endDrag();
        if (m_points.size() < 3) {
            m_points.clear();
            return;
        }
        QPolygon poly;
        poly.reserve(m_points.size());
        for (const QPoint& pt : m_points)
            poly.append(pt);
        m_points.clear();
        QPainterPath path;
        path.addPolygon(poly);
        path.closeSubpath();
        Selection s = maskFromPath(document()->bounds(), path);
        if (commitSelection(document(), s, mode, name()))
            requestUpdate(document()->bounds());
    }

    void drawOverlay(QPainter& p) override
    {
        if (!m_dragging || m_points.size() < 2)
            return;
        const QRectF box = QRectF(QPointF(m_points.first()), QPointF(m_points.last())).normalized();
        const bool cheap = box.width() * box.height() < 2.0e6;
        if (cheap) {
            const QRect area = box.toRect().adjusted(-2, -2, 2, 2).intersected(document()->bounds());
            QPainterPath path;
            path.moveTo(m_points.first());
            for (int i = 1; i < m_points.size(); ++i)
                path.lineTo(m_points[i]);
            path.closeSubpath();
            drawLocalAnts(p, maskFromPath(area, path), area, viewZoom(this));
        }
        drawDashedLine(p, QPointF(m_points.last()), QPointF(m_cur), QColor(255, 255, 255, 200));
    }

private:
    QVector<QPoint> m_points;
};

// -------------------------------------------------------------- magic wand

class MagicWandTool : public Tool
{
public:
    explicit MagicWandTool(QObject* parent) : Tool(parent) {}

    QString id() const override { return QStringLiteral("wand"); }
    QString name() const override { return tr("Magic Wand"); }
    QString toolTip() const override
    {
        return tr("Click to select an area of similar colours (hold Ctrl for a global selection)");
    }
    QString shortcutString() const override { return QStringLiteral("Y"); }
    int sortOrder() const override { return 103; }

    QString statusText() const override
    {
        return tr("Tolerance: %1, %2")
            .arg(m_tolerance)
            .arg(m_contiguous ? tr("contiguous") : tr("global"));
    }

    QWidget* createOptionsWidget(QWidget* parent) override
    {
        QBoxLayout* layout = nullptr;
        QWidget* root = ToolOptions::createRoot(parent, &layout);
        m_modeBox = qobject_cast<QComboBox*>(ToolOptions::addCombo(
            layout, tr("Mode"),
            { tr("New"), tr("Add"), tr("Subtract"), tr("Intersect") }, 0));
        connect(m_modeBox, &QComboBox::currentIndexChanged, this, [this](int i) {
            m_mode = i;
            refreshStatus();
        });
        m_tolRow = ToolOptions::addSlider(layout, tr("Tolerance"), 0, 255, m_tolerance);
        connect(m_tolRow.slider, &QSlider::valueChanged, this, [this](int v) {
            m_tolerance = v;
            refreshStatus();
        });
        m_contiguousBox = qobject_cast<QCheckBox*>(ToolOptions::addCheck(layout, tr("Contiguous"), true));
        connect(m_contiguousBox, &QCheckBox::toggled, this, [this](bool v) {
            m_contiguous = v;
            refreshStatus();
        });
        m_optionsWidget = root;
        return root;
    }

    void mouseDown(const QPoint&, Qt::MouseButton, Qt::KeyboardModifiers) override {}

    void mouseMove(const QPoint&, Qt::MouseButtons, Qt::KeyboardModifiers) override {}

    void mouseUp(const QPoint& docPos, Qt::MouseButton button, Qt::KeyboardModifiers mods) override
    {
        if (!document() || !document()->bounds().contains(docPos))
            return;
        const QImage composite = document()->compositeImage();
        if (composite.isNull())
            return;
        const bool contiguous = (mods & Qt::ControlModifier) ? false : m_contiguous;
        Selection s(document()->width(), document()->height());
        s.magicWand(composite, docPos, m_tolerance, contiguous);
        int mode = m_mode;
        if (button == Qt::RightButton)
            mode = 2;
        if (commitSelection(document(), s, mode, name()))
            requestUpdate(document()->bounds());
    }

    void drawOverlay(QPainter&) override {}

private:
    int m_mode = 0;
    int m_tolerance = 32;
    bool m_contiguous = true;
    QComboBox* m_modeBox = nullptr;
    QCheckBox* m_contiguousBox = nullptr;
    ToolOptions::SliderRow m_tolRow;
};

// -------------------------------------------------------- select opaque

class SelectOpaqueTool : public Tool
{
public:
    explicit SelectOpaqueTool(QObject* parent) : Tool(parent) {}

    QString id() const override { return QStringLiteral("selecttransparent"); }
    QString name() const override { return tr("Select Opaque/Transparent"); }
    QString toolTip() const override
    {
        return tr("Click to select the fully transparent (or opaque) pixels of the image");
    }
    QString shortcutString() const override { return QStringLiteral("Ctrl+Shift+T"); }
    int sortOrder() const override { return 104; }

    QString statusText() const override { return tr("Click the image to select"); }

    QWidget* createOptionsWidget(QWidget* parent) override
    {
        QBoxLayout* layout = nullptr;
        QWidget* root = ToolOptions::createRoot(parent, &layout);
        m_kindBox = qobject_cast<QComboBox*>(ToolOptions::addCombo(
            layout, tr("Select"), { tr("Opaque"), tr("Transparent") }, 0));
        connect(m_kindBox, &QComboBox::currentIndexChanged, this, [this](int i) {
            m_kind = i;
            refreshStatus();
        });
        m_modeBox = qobject_cast<QComboBox*>(ToolOptions::addCombo(
            layout, tr("Mode"),
            { tr("New"), tr("Add"), tr("Subtract"), tr("Intersect") }, 0));
        connect(m_modeBox, &QComboBox::currentIndexChanged, this, [this](int i) {
            m_mode = i;
            refreshStatus();
        });
        m_optionsWidget = root;
        return root;
    }

    void mouseUp(const QPoint&, Qt::MouseButton button, Qt::KeyboardModifiers) override
    {
        if (!document())
            return;
        const QImage composite = document()->compositeImage();
        if (composite.isNull())
            return;
        Selection s(document()->width(), document()->height());
        if (m_kind == 0)
            s.selectOpaque(composite);
        else
            s.selectTransparent(composite);
        int mode = m_mode;
        if (button == Qt::RightButton)
            mode = 2;
        if (commitSelection(document(), s, mode, name()))
            requestUpdate(document()->bounds());
    }

private:
    int m_kind = 0;
    int m_mode = 0;
    QComboBox* m_kindBox = nullptr;
    QComboBox* m_modeBox = nullptr;
};

// --------------------------------------------------------- move selected

/// "Move Selected": drags the pixels of the current selection around inside
/// the selection (a floating selection) or the whole layer when there is no
/// selection. Preview = restore the snapshot, then blit it with the offset.
class MoveSelectedTool : public Tool
{
public:
    explicit MoveSelectedTool(QObject* parent) : Tool(parent) {}

    QString id() const override { return QStringLiteral("move"); }
    QString name() const override { return tr("Move Selected"); }
    QString toolTip() const override
    {
        return tr("Move the selected pixels (or the whole layer) with the mouse or the arrow keys");
    }
    QString shortcutString() const override { return QStringLiteral("Ctrl+M"); }
    int sortOrder() const override { return 105; }
    Qt::CursorShape cursorShape() const override { return Qt::SizeAllCursor; }

    QString statusText() const override
    {
        if (!m_haveSnap)
            return tr("Drag to move the selection contents");
        return tr("Offset: %1, %2").arg(m_offset.x()).arg(m_offset.y());
    }

    QWidget* createOptionsWidget(QWidget* parent) override
    {
        QBoxLayout* layout = nullptr;
        QWidget* root = ToolOptions::createRoot(parent, &layout);
        ToolOptions::addSeparator(layout);
        QLabel* hint = new QLabel(tr("Arrow keys move by 1 px, Shift+Arrow by 10 px."), root);
        hint->setWordWrap(true);
        layout->addWidget(hint);
        m_optionsWidget = root;
        return root;
    }

    void activate() override
    {
        reset();
        Tool::activate();
    }

    void deactivate() override
    {
        reset();
        Tool::deactivate();
    }

    void mouseDown(const QPoint& docPos, Qt::MouseButton, Qt::KeyboardModifiers) override
    {
        if (!beginSession())
            return;
        m_press = docPos;
        m_cur = docPos;
        m_dragging = true;
        m_offset = QPoint(0, 0);
        render();
    }

    void mouseMove(const QPoint& docPos, Qt::MouseButtons, Qt::KeyboardModifiers) override
    {
        if (!m_dragging)
            return;
        m_cur = docPos;
        m_offset = m_cur - m_press;
        render();
        refreshStatus();
    }

    void mouseUp(const QPoint& docPos, Qt::MouseButton, Qt::KeyboardModifiers) override
    {
        if (!m_dragging) {
            reset();
            return;
        }
        m_cur = docPos;
        m_offset = m_cur - m_press;
        m_dragging = false;
        if (m_offset == QPoint(0, 0)) {
            // Nothing moved: simply drop the snapshot.
            reset();
            return;
        }
        render();
        commit(tr("Move Selected"));
    }

    void keyPressEvent(QKeyEvent* e) override
    {
        const int step = (e->modifiers() & Qt::ShiftModifier) ? 10 : 1;
        QPoint delta(0, 0);
        switch (e->key()) {
        case Qt::Key_Left:
            delta = QPoint(-step, 0);
            break;
        case Qt::Key_Right:
            delta = QPoint(step, 0);
            break;
        case Qt::Key_Up:
            delta = QPoint(0, -step);
            break;
        case Qt::Key_Down:
            delta = QPoint(0, step);
            break;
        case Qt::Key_Escape:
            if (m_haveSnap)
                abort();
            e->accept();
            return;
        default:
            Tool::keyPressEvent(e);
            return;
        }
        e->accept();
        if (m_dragging) {
            m_offset += delta;
            m_press += delta;
            render();
            refreshStatus();
            return;
        }
        if (!beginSession())
            return;
        m_offset = delta;
        render();
        commit(tr("Move Selected"));
    }

    void drawOverlay(QPainter& p) override
    {
        if (!m_dragging || !m_haveSnap)
            return;
        const QColor c = toQColor(primaryColor());
        const QRectF src(m_workRect);
        const QRectF dst = src.translated(m_offset);
        p.save();
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(c.red(), c.green(), c.blue(), 60));
        p.drawRect(dst);
        p.restore();
        drawDashedRect(p, dst, QColor(255, 255, 255, 220));
        drawDashedLine(p, QPointF(m_workRect.center()) + QPointF(0.5, 0.5),
                       QPointF(m_workRect.center()) + QPointF(0.5, 0.5) + QPointF(m_offset),
                       QColor(255, 255, 255, 140));
    }

private:
    void reset()
    {
        m_haveSnap = false;
        m_dragging = false;
        m_before = Surface();
        m_snap = Surface();
        m_mask = QImage();
        m_workRect = QRect();
        m_offset = QPoint(0, 0);
        m_layerIndex = -1;
        requestOverlayUpdate();
    }

    /// Captures the pixels to be moved plus the untouched "before" state.
    bool beginSession()
    {
        reset();
        if (!document() || !canPaint())
            return false;
        Layer* l = activeLayer();
        if (!l)
            return false;
        m_layerIndex = document()->activeLayerIndex();
        m_floatSel = document()->hasSelection();
        const QRect base = m_floatSel ? document()->selection().nonEmptyRect() : l->bounds();
        if (base.isEmpty())
            return false;
        m_workRect = base;
        m_before = l->surface().cropped(base);
        m_snap = m_before.copy();
        m_mask = QImage();
        if (m_floatSel) {
            Selection local(base.width(), base.height());
            local.setMask(document()->selection().mask().copy(base));
            Renderer::applyMask(m_snap, local, true);
            m_mask = local.mask();
        }
        m_haveSnap = true;
        return true;
    }

    void render()
    {
        if (!m_haveSnap || !document())
            return;
        Layer* l = document()->layerAt(m_layerIndex);
        if (!l)
            return;
        // A floating selection: the captured pixels are lifted out of the
        // layer (the area they vacate becomes transparent) and dropped again
        // at the current offset - still restricted to the selection.
        const QImage before = m_before.toQImageConst();
        const QImage snap = m_snap.toQImageConst();
        Surface& s = l->surface();
        for (int y = m_workRect.top(); y <= m_workRect.bottom(); ++y) {
            const int ly = y - m_workRect.top();
            if (ly < 0 || ly >= before.height())
                continue;
            pixel_t* row = s.scanLine(y);
            const pixel_t* brow = reinterpret_cast<const pixel_t*>(before.constScanLine(ly));
            const uchar* mv = m_mask.isNull() ? nullptr : m_mask.constScanLine(ly);
            for (int x = m_workRect.left(); x <= m_workRect.right(); ++x) {
                const int lx = x - m_workRect.left();
                if (lx < 0 || lx >= before.width())
                    continue;
                const int v = mv ? mv[lx] : 255;
                // 1. lift: only the pixels outside the selection stay in place
                row[x] = v >= 255 ? 0u : scalePixel(brow[lx], 255 - v);
                // 2. drop the snapshot at the offset
                const int sx = lx - m_offset.x();
                const int sy = ly - m_offset.y();
                if (sx < 0 || sy < 0 || sx >= snap.width() || sy >= snap.height())
                    continue;
                pixel_t sp = reinterpret_cast<const pixel_t*>(snap.constScanLine(sy))[sx];
                if (sp == 0)
                    continue;
                if (v != 255)
                    sp = scalePixel(sp, v);
                row[x] = composePixel(row[x], sp, BlendMode::Normal);
            }
        }
        document()->notifyLayerPixels(m_layerIndex, m_workRect);
        requestUpdate(m_workRect);
        requestOverlayUpdate();
    }

    void commit(const QString& actionName)
    {
        if (!m_haveSnap || !document())
            return;
        Layer* l = document()->layerAt(m_layerIndex);
        const QRect dirty = m_workRect;
        if (l) {
            document()->notifyLayerPixels(m_layerIndex, dirty);
            pushShapeAction(this, actionName, m_layerIndex, dirty, m_before,
                            l->surface().cropped(dirty));
        }
        reset();
        requestUpdate(dirty);
    }

    void abort()
    {
        if (m_haveSnap)
            restoreRegion(this, document(), m_layerIndex, m_workRect, m_before);
        reset();
    }

    QPoint m_press;
    QPoint m_cur;
    QPoint m_offset;
    QRect m_workRect;
    Surface m_before;
    Surface m_snap;
    QImage m_mask;
    int m_layerIndex = -1;
    bool m_haveSnap = false;
    bool m_dragging = false;
    bool m_floatSel = false;
};

} // namespace

// ------------------------------------------------------------------ factory

QList<Tool*> createSelectionTools()
{
    return { new RectangularSelectionTool(nullptr), new EllipticalSelectionTool(nullptr),
             new FreeformSelectionTool(nullptr),    new MagicWandTool(nullptr),
             new SelectOpaqueTool(nullptr),         new MoveSelectedTool(nullptr) };
}

} // namespace pnq
