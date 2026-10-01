// Shape tools: line, rectangle, ellipse, polygon, curve and freeform shape.
//
// Like Paint.NET every shape is previewed in drawOverlay() while dragging and
// only rasterised into the layer on mouse-up - as a single history step.

#include "tools/Tool.h"

#include "core/Document.h"
#include "core/History.h"
#include "core/ImageMath.h"
#include "core/Selection.h"
#include "core/Surface.h"
#include "tools/ToolManager.h"
#include "tools/ToolSupport.h"
#include "ui/CanvasView.h"
#include "ui/ToolOptions.h"

#include <QCheckBox>
#include <QComboBox>
#include <QKeyEvent>
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

/// Rasterises a path into a full canvas selection mask. Done here instead of
/// Selection::selectPath(), which resizes the mask to the path's bounding box.
Selection selectionFromPath(const Document* doc, const QPainterPath& path)
{
    Selection out;
    QImage m(doc->width(), doc->height(), QImage::Format_Alpha8);
    m.fill(0);
    {
        QPainter p(&m);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(255, 255, 255, 255));
        p.drawPath(path);
    }
    out.setMask(m);
    return out;
}

/// Inclusive pixel rectangle between two drag points.
QRect dragRect(const QPoint& a, const QPoint& b) { return QRect(a, b).normalized(); }
QRectF dragRectF(const QPoint& a, const QPoint& b)
{
    return QRectF(QPointF(a), QPointF(b)).normalized().adjusted(0, 0, 1, 1);
}

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

/// Lightens (amount > 0) or darkens (amount < 0) a colour; used by the "3D"
/// pseudo extrusion of the polygon tool.
pixel_t shadeColor(pixel_t c, double amount)
{
    const QColor q = toQColor(c);
    const double a = qBound(-1.0, amount, 1.0);
    const QColor target = a >= 0 ? QColor(255, 255, 255, q.alpha()) : QColor(0, 0, 0, q.alpha());
    const int t = int(std::abs(a) * 255.0);
    return toPixel(QColor(q.red() + (target.red() - q.red()) * t / 255,
                          q.green() + (target.green() - q.green()) * t / 255,
                          q.blue() + (target.blue() - q.blue()) * t / 255, q.alpha()));
}

/// Commits a pixel change as exactly one, non mergeable history step.
/// PixelDeltaAction::isMergeable() is true, so plain pushPixelAction() calls
/// would be merged with the previous one and one Ctrl+Z would revert several
/// unrelated shapes; the macro wrapper keeps the steps apart.
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

// -------------------------------------------------------------- shape base

/// Common behaviour: option widgets, fill/outline handling, selection modes
/// and the single "paint the shape" routine.
class ShapeToolBase : public Tool
{
public:
    explicit ShapeToolBase(QObject* parent) : Tool(parent) {}

    enum DrawMode { Fill = 0, Outline, FillAndOutline };
    enum SelMode { SelNew = 0, SelAdd, SelSubtract, SelIntersect };

    void deactivate() override
    {
        m_dragging = false;
        Tool::deactivate();
    }

protected:
    static QStringList modeNames()
    {
        return { tr("Fill"), tr("Outline"), tr("Fill + Outline") };
    }

    /// Builds the standard shape options bar. `rounded` adds the rounded
    /// rectangle controls, `defaultWidth` the outline width.
    void buildOptions(QWidget* parent, bool rounded = true, int defaultWidth = 1)
    {
        QBoxLayout* layout = nullptr;
        m_optionsWidget = ToolOptions::createRoot(parent, &layout);
        m_layout = layout;

        m_modeBox = qobject_cast<QComboBox*>(ToolOptions::addCombo(layout, tr("Mode"), modeNames(), m_mode));
        connect(m_modeBox, &QComboBox::currentIndexChanged, this, [this](int i) {
            m_mode = i;
            requestOverlayUpdate();
        });

        m_widthBox = qobject_cast<QSpinBox*>(
            ToolOptions::addSpin(layout, tr("Outline width"), 1, 64, defaultWidth, tr(" px")));
        connect(m_widthBox, &QSpinBox::valueChanged, this, [this](int v) {
            m_width = v;
            requestOverlayUpdate();
        });

        if (rounded) {
            m_roundedBox = qobject_cast<QCheckBox*>(ToolOptions::addCheck(layout, tr("Rounded corners"), false));
            connect(m_roundedBox, &QCheckBox::toggled, this, [this](bool v) {
                m_rounded = v;
                requestOverlayUpdate();
            });
            m_radiusBox = qobject_cast<QSpinBox*>(
                ToolOptions::addSpin(layout, tr("Corner radius"), 1, 200, 8, tr(" px")));
            connect(m_radiusBox, &QSpinBox::valueChanged, this, [this](int v) {
                m_radius = v;
                requestOverlayUpdate();
            });
        }

        ToolOptions::addSeparator(layout);
        m_selBox = qobject_cast<QComboBox*>(ToolOptions::addCombo(
            layout, tr("Selection mode"),
            { tr("New"), tr("Add"), tr("Subtract"), tr("Intersect") }, m_selMode));
        connect(m_selBox, &QComboBox::currentIndexChanged, this, [this](int i) {
            m_selMode = i;
            requestOverlayUpdate();
        });
    }

    void beginDrag(const QPoint& p, Qt::MouseButton b)
    {
        m_press = p;
        m_cur = p;
        m_dragging = true;
        m_button = b;
    }

    bool doFill() const { return m_mode == Fill || m_mode == FillAndOutline; }
    bool doOutline() const { return m_mode == Outline || m_mode == FillAndOutline; }
    pixel_t fillColor() const
    {
        return m_button == Qt::RightButton ? secondaryColor() : primaryColor();
    }
    pixel_t lineColor() const
    {
        // Left button paints the primary colour, right button the secondary one,
        // the same rule the brush and the fill tools follow. This was swapped,
        // which drew shape outlines in white on a white canvas.
        return m_button == Qt::RightButton ? secondaryColor() : primaryColor();
    }

    /// The selection the shape is clipped to, or nullptr when the shape may
    /// be drawn anywhere.
    const Selection* clipSelection() const
    {
        if (m_selMode == SelAdd || m_selMode == SelSubtract)
            return nullptr;
        if (!document() || document()->selection().isNull())
            return nullptr;
        return &document()->selection();
    }

    /// Rasterises the shape into the active layer and records one undo step.
    void paintShape(const QPainterPath& path, const QString& actionName)
    {
        Document* doc = document();
        if (!doc || !canPaint())
            return;
        Layer* l = activeLayer();
        if (!l)
            return;
        const int pad = m_width + 2;
        const QRect area = path.boundingRect().toRect().adjusted(-pad, -pad, pad, pad);
        const QRect clipArea = area.intersected(l->bounds());
        if (clipArea.isEmpty())
            return;
        const int layerIndex = doc->activeLayerIndex();
        const pixel_t fcol = fillColor();
        const pixel_t lcol = lineColor();
        const bool f = doFill();
        const bool o = doOutline();
        const int width = m_width;
        const Selection* sel = clipSelection();

        StrokeBuffer buf;
        buf.begin(&l->surface());
        buf.touch(area);
        if (f)
            ImageMath::fillPath(l->surface(), path, fcol, sel, 1.0);
        if (o)
            ImageMath::strokePath(l->surface(), path, lcol, sel, 1.0, width, true);
        const QRect dirty = buf.end();
        if (dirty.isEmpty())
            return;
        const Surface before = buf.before();
        const Surface after = buf.after();
        if (before == after)
            return;  // completely outside the selection: nothing to undo
        l->markThumbnailDirty();
        doc->notifyLayerPixels(layerIndex, dirty);
        pushShapeAction(this, actionName, layerIndex, dirty, before, after);
        requestUpdate(dirty);
        updateSelectionFromPath(path);
    }

    /// "Add/Subtract/Intersect" also merges the shape's own area into the
    /// document selection (Paint.NET behaviour).
    void updateSelectionFromPath(const QPainterPath& path)
    {
        Document* doc = document();
        if (!doc || m_selMode == SelNew)
            return;
        Selection region = selectionFromPath(doc, path);
        const Selection before = deepCopySelection(doc->selection());
        Selection after = before.isNull() || before.isEmpty()
                              ? Selection(doc->width(), doc->height(), false)
                              : before;
        if (m_selMode == SelAdd)
            after += region;
        else if (m_selMode == SelSubtract)
            after -= region;
        else
            after &= region;
        if (after.isEmpty())
            after = Selection();
        if (after == before)
            return;
        doc->setSelection(after);
        if (doc->history())
            doc->history()->push(new SelectionAction(tr("Selection"), before, deepCopySelection(after)));
        requestUpdate(doc->bounds());
    }

    /// Overlay rendering shared by all shape previews.
    void drawShapePreview(QPainter& p, const QPainterPath& path) const
    {
        p.save();
        p.setRenderHint(QPainter::Antialiasing, true);
        if (doFill()) {
            QColor c = toQColor(fillColor());
            c.setAlpha(90);
            p.setPen(Qt::NoPen);
            p.setBrush(c);
            p.drawPath(path);
        }
        if (doOutline()) {
            QPen pen(toQColor(lineColor()));
            pen.setCosmetic(true);
            pen.setWidthF(qBound(1.0, qMin(double(m_width), 3.0), 3.0));
            p.setPen(pen);
            p.setBrush(Qt::NoBrush);
            p.drawPath(path);
        }
        p.restore();
    }

    int m_mode = FillAndOutline;
    int m_width = 1;
    int m_radius = 8;
    int m_selMode = SelNew;
    bool m_rounded = false;
    bool m_dragging = false;
    Qt::MouseButton m_button = Qt::LeftButton;
    QPoint m_press;
    QPoint m_cur;
    QBoxLayout* m_layout = nullptr;
    QComboBox* m_modeBox = nullptr;
    QComboBox* m_selBox = nullptr;
    QSpinBox* m_widthBox = nullptr;
    QSpinBox* m_radiusBox = nullptr;
    QCheckBox* m_roundedBox = nullptr;
};

// -------------------------------------------------------------------- line

class LineTool : public ShapeToolBase
{
public:
    explicit LineTool(QObject* parent) : ShapeToolBase(parent) {}

    QString id() const override { return QStringLiteral("line"); }
    QString name() const override { return tr("Line"); }
    QString toolTip() const override { return tr("Draw a straight line"); }
    QString shortcutString() const override { return QStringLiteral("L"); }
    int sortOrder() const override { return 200; }

    QString statusText() const override
    {
        if (!m_dragging)
            return QString();
        return tr("%1, %2").arg(m_cur.x() - m_press.x()).arg(m_cur.y() - m_press.y());
    }

    QWidget* createOptionsWidget(QWidget* parent) override
    {
        buildOptions(parent, false, qBound(1, brush().size(), 64));
        return m_optionsWidget;
    }

    void mouseDown(const QPoint& docPos, Qt::MouseButton button, Qt::KeyboardModifiers) override
    {
        beginDrag(docPos, button);
        requestOverlayUpdate();
    }

    void mouseMove(const QPoint& docPos, Qt::MouseButtons, Qt::KeyboardModifiers mods) override
    {
        if (!m_dragging)
            return;
        m_cur = docPos;
        if (mods & Qt::ShiftModifier) {
            // Constrain to 45 degree steps.
            const int dx = docPos.x() - m_press.x();
            const int dy = docPos.y() - m_press.y();
            const int adx = qAbs(dx), ady = qAbs(dy);
            if (adx > ady * 2)
                m_cur.setY(m_press.y());
            else if (ady > adx * 2)
                m_cur.setX(m_press.x());
            else
                m_cur.setY(m_press.y() + (dx < 0 ? -ady : ady));
        }
        requestOverlayUpdate();
        refreshStatus();
    }

    void mouseUp(const QPoint& docPos, Qt::MouseButton, Qt::KeyboardModifiers) override
    {
        if (!m_dragging)
            return;
        m_cur = docPos;
        m_dragging = false;
        if (!document() || !canPaint())
            return;
        Layer* l = activeLayer();
        if (!l)
            return;
        const int pad = m_width + 2;
        QRect area = ImageMath::lineRect(m_press, m_cur).adjusted(-pad, -pad, pad, pad);
        area = area.intersected(l->bounds());
        if (area.isEmpty())
            return;
        const int layerIndex = document()->activeLayerIndex();
        const pixel_t col = lineColor();
        const Selection* sel = clipSelection();
        const int width = m_width;
        const QPointF p0(m_press);
        const QPointF p1(m_cur);
        StrokeBuffer buf;
        buf.begin(&l->surface());
        buf.touch(area);
        ImageMath::drawSmoothLine(l->surface(), p0, p1, col, sel, 1.0, width);
        const QRect dirty = buf.end();
        if (dirty.isEmpty())
            return;
        const Surface before = buf.before();
        const Surface after = buf.after();
        if (before == after)
            return;
        l->markThumbnailDirty();
        document()->notifyLayerPixels(layerIndex, dirty);
        pushShapeAction(this, name(), layerIndex, dirty, before, after);
        requestUpdate(dirty);
    }

    void drawOverlay(QPainter& p) override
    {
        if (!m_dragging)
            return;
        drawDashedLine(p, QPointF(m_press), QPointF(m_cur), toQColor(lineColor()));
    }
};

// --------------------------------------------------------------- rectangle

class RectangleTool : public ShapeToolBase
{
public:
    explicit RectangleTool(QObject* parent) : ShapeToolBase(parent) {}

    QString id() const override { return QStringLiteral("rectangle"); }
    QString name() const override { return tr("Rectangle"); }
    QString toolTip() const override { return tr("Draw a rectangle"); }
    QString shortcutString() const override { return QStringLiteral("R"); }
    int sortOrder() const override { return 201; }

    QString statusText() const override
    {
        if (!m_dragging)
            return QString();
        const QRect r = dragRect(m_press, m_cur);
        return tr("%1 x %2 at %3, %4").arg(r.width()).arg(r.height()).arg(r.left()).arg(r.top());
    }

    QWidget* createOptionsWidget(QWidget* parent) override
    {
        buildOptions(parent, true);
        return m_optionsWidget;
    }

    QPainterPath currentPath(Qt::KeyboardModifiers mods = Qt::NoModifier) const
    {
        QRect r = dragRect(m_press, m_cur);
        if (mods & Qt::ShiftModifier) {
            const int s = qMax(r.width(), r.height());
            r = QRect(r.left(), r.top(), s, s);
        }
        QPainterPath path;
        if (m_rounded)
            path.addRoundedRect(QRectF(r), qMin(double(m_radius), qMin(r.width(), r.height()) / 2.0),
                               qMin(double(m_radius), qMin(r.width(), r.height()) / 2.0));
        else
            path.addRect(QRectF(r));
        return path;
    }

    void mouseDown(const QPoint& docPos, Qt::MouseButton button, Qt::KeyboardModifiers) override
    {
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

    void mouseUp(const QPoint& docPos, Qt::MouseButton, Qt::KeyboardModifiers mods) override
    {
        if (!m_dragging)
            return;
        m_cur = docPos;
        m_dragging = false;
        if (dragRect(m_press, m_cur).isEmpty())
            return;
        paintShape(currentPath(mods), name());
    }

    void drawOverlay(QPainter& p) override
    {
        if (!m_dragging)
            return;
        drawShapePreview(p, currentPath());
        drawDashedRect(p, dragRectF(m_press, m_cur), QColor(255, 255, 255, 160));
    }
};

// ----------------------------------------------------------------- ellipse

class EllipseTool : public ShapeToolBase
{
public:
    explicit EllipseTool(QObject* parent) : ShapeToolBase(parent) {}

    QString id() const override { return QStringLiteral("ellipse"); }
    QString name() const override { return tr("Ellipse"); }
    QString toolTip() const override { return tr("Draw an ellipse"); }
    QString shortcutString() const override { return QStringLiteral("Shift+R"); }
    int sortOrder() const override { return 202; }

    QString statusText() const override
    {
        if (!m_dragging)
            return QString();
        const QRect r = dragRect(m_press, m_cur);
        return tr("%1 x %2 at %3, %4").arg(r.width()).arg(r.height()).arg(r.left()).arg(r.top());
    }

    QWidget* createOptionsWidget(QWidget* parent) override
    {
        buildOptions(parent, false);
        return m_optionsWidget;
    }

    QPainterPath currentPath(Qt::KeyboardModifiers mods = Qt::NoModifier) const
    {
        QRect r = dragRect(m_press, m_cur);
        if (mods & Qt::ShiftModifier) {
            const int s = qMax(r.width(), r.height());
            r = QRect(r.left(), r.top(), s, s);
        }
        QPainterPath path;
        path.addEllipse(QRectF(r));
        return path;
    }

    void mouseDown(const QPoint& docPos, Qt::MouseButton button, Qt::KeyboardModifiers) override
    {
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

    void mouseUp(const QPoint& docPos, Qt::MouseButton, Qt::KeyboardModifiers mods) override
    {
        if (!m_dragging)
            return;
        m_cur = docPos;
        m_dragging = false;
        if (dragRect(m_press, m_cur).isEmpty())
            return;
        paintShape(currentPath(mods), name());
    }

    void drawOverlay(QPainter& p) override
    {
        if (!m_dragging)
            return;
        drawShapePreview(p, currentPath());
        p.save();
        p.setPen(Qt::NoPen);
        p.setBrush(Qt::NoBrush);
        QPen pen(QColor(255, 255, 255, 160));
        pen.setCosmetic(true);
        pen.setStyle(Qt::DashLine);
        pen.setDashPattern({ 4.0, 3.0 });
        p.setPen(pen);
        p.drawEllipse(dragRectF(m_press, m_cur));
        p.restore();
    }
};

// ----------------------------------------------------------------- polygon

class PolygonTool : public ShapeToolBase
{
public:
    explicit PolygonTool(QObject* parent) : ShapeToolBase(parent) {}

    QString id() const override { return QStringLiteral("polygon"); }
    QString name() const override { return tr("Polygon"); }
    QString toolTip() const override
    {
        return tr("Click to add points, double click (or Enter) to close the polygon");
    }
    QString shortcutString() const override { return QStringLiteral("Shift+P"); }
    int sortOrder() const override { return 203; }

    QString statusText() const override
    {
        if (m_points.isEmpty())
            return tr("Click to place the first point");
        return tr("%1 points - double click to close").arg(m_points.size());
    }

    QWidget* createOptionsWidget(QWidget* parent) override
    {
        buildOptions(parent, false);
        m_3dBox = qobject_cast<QCheckBox*>(ToolOptions::addCheck(m_layout, tr("3D"), false));
        connect(m_3dBox, &QCheckBox::toggled, this, [this](bool v) {
            m_3d = v;
            requestOverlayUpdate();
        });
        return m_optionsWidget;
    }

    void activate() override
    {
        m_points.clear();
        ShapeToolBase::activate();
    }

    void deactivate() override
    {
        m_points.clear();
        requestOverlayUpdate();
        ShapeToolBase::deactivate();
    }

    void mouseDown(const QPoint& docPos, Qt::MouseButton button, Qt::KeyboardModifiers) override
    {
        if (!document())
            return;
        m_button = button;
        if (m_points.size() >= 3 && (docPos - m_points.first()).manhattanLength() <= 4) {
            finish();
            return;
        }
        if (!m_points.isEmpty() && (docPos - m_points.last()).manhattanLength() < 1)
            return;
        m_points.append(docPos);
        m_press = docPos;
        m_cur = docPos;
        emit sessionChanged(true);
        requestOverlayUpdate();
        refreshStatus();
    }

    void mouseMove(const QPoint& docPos, Qt::MouseButtons, Qt::KeyboardModifiers) override
    {
        if (m_points.isEmpty())
            return;
        m_cur = docPos;
        requestOverlayUpdate();
    }

    void mouseUp(const QPoint& docPos, Qt::MouseButton, Qt::KeyboardModifiers) override
    {
        m_cur = docPos;
        requestOverlayUpdate();
    }

    void mouseDoubleClick(const QPoint& docPos, Qt::MouseButton, Qt::KeyboardModifiers) override
    {
        m_cur = docPos;
        finish();
    }

    void keyPressEvent(QKeyEvent* e) override
    {
        if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) {
            finish();
            e->accept();
            return;
        }
        if (e->key() == Qt::Key_Escape) {
            m_points.clear();
            requestOverlayUpdate();
            e->accept();
            return;
        }
        if (e->key() == Qt::Key_Backspace && !m_points.isEmpty()) {
            m_points.removeLast();
            requestOverlayUpdate();
            e->accept();
            return;
        }
        ShapeToolBase::keyPressEvent(e);
    }

    void drawOverlay(QPainter& p) override
    {
        if (m_points.isEmpty())
            return;
        const QColor c = toQColor(fillColor());
        p.save();
        QPen pen(c);
        pen.setCosmetic(true);
        p.setPen(pen);
        for (int i = 1; i < m_points.size(); ++i)
            p.drawLine(m_points[i - 1], m_points[i]);
        p.restore();
        drawDashedLine(p, QPointF(m_points.last()), QPointF(m_cur), QColor(255, 255, 255, 200));
        for (const QPoint& pt : m_points) {
            p.save();
            p.setPen(QPen(QColor(0, 0, 0, 200)));
            p.setBrush(c);
            p.drawRect(QRectF(pt.x() - 1.5, pt.y() - 1.5, 3, 3));
            p.restore();
        }
    }

private:
    QPainterPath buildPath(const QVector<QPoint>& pts) const
    {
        QPainterPath path;
        if (pts.size() < 2)
            return path;
        path.moveTo(pts.first());
        for (int i = 1; i < pts.size(); ++i)
            path.lineTo(pts[i]);
        path.closeSubpath();
        return path;
    }

    bool finishSession() override { finish(); return true; }

    void finish()
    {
        if (m_points.size() < 2) {
            m_points.clear();
            emit sessionChanged(false);
            requestOverlayUpdate();
            return;
        }
        const QVector<QPoint> pts = m_points;
        const QPainterPath path = buildPath(pts);
        m_points.clear();
        emit sessionChanged(false);
        requestOverlayUpdate();
        paintShape3D(path, pts);
    }

    /// Optional pseudo-3D: the body is filled and every edge is shaded
    /// according to how much it faces the (top-left) light source.
    void paintShape3D(const QPainterPath& path, const QVector<QPoint>& pts)
    {
        if (!m_3d) {
            paintShape(path, name());
            return;
        }
        const int saved = m_mode;
        m_mode = Fill;
        paintShape(path, name());
        m_mode = saved;
        if (!doOutline() || !document() || !canPaint())
            return;
        Layer* l = activeLayer();
        if (!l)
            return;
        // Signed area to know whether the polygon is clockwise in screen space.
        double area2 = 0.0;
        for (int i = 0; i < pts.size(); ++i) {
            const QPointF& a = pts[i];
            const QPointF& b = pts[(i + 1) % pts.size()];
            area2 += a.x() * b.y() - b.x() * a.y();
        }
        const double sign = area2 >= 0.0 ? 1.0 : -1.0;
        const int layerIndex = document()->activeLayerIndex();
        const pixel_t base = fillColor();
        const int width = m_width;
        const int pad = width + 2;
        StrokeBuffer buf;
        buf.begin(&l->surface());
        for (int i = 0; i < pts.size(); ++i) {
            const QPointF& a = pts[i];
            const QPointF& b = pts[(i + 1) % pts.size()];
            const double dx = b.x() - a.x(), dy = b.y() - a.y();
            const double len = std::hypot(dx, dy);
            if (len < 0.001)
                continue;
            // Outward normal of the edge.
            const double nx = (dy / len) * sign;
            const double ny = (-dx / len) * sign;
            // Light comes from the top-left.
            const double facing = -(nx * -0.7071 + ny * -0.7071);
            const pixel_t col = shadeColor(base, facing * 0.7);
            QPainterPath edge;
            edge.moveTo(a);
            edge.lineTo(b);
            buf.touch(QRectF(a, b).toRect().adjusted(-pad, -pad, pad, pad));
            ImageMath::strokePath(l->surface(), edge, col, clipSelection(), 1.0, width, true);
        }
        const QRect dirty = buf.end();
        if (dirty.isEmpty())
            return;
        const Surface before = buf.before();
        const Surface after = buf.after();
        if (before == after)
            return;
        l->markThumbnailDirty();
        document()->notifyLayerPixels(layerIndex, dirty);
        pushShapeAction(this, name(), layerIndex, dirty, before, after);
        requestUpdate(dirty);
    }

    QVector<QPoint> m_points;
    bool m_3d = false;
    QCheckBox* m_3dBox = nullptr;
};

// ------------------------------------------------------------------- curve

/// Clicks add nodes, dragging a node pulls its tangent, the smoothed
/// cubic path is rasterised on release. The stroke is committed when the
/// curve is finished (double click / Enter / tool switch).
class CurveTool : public ShapeToolBase
{
public:
    explicit CurveTool(QObject* parent) : ShapeToolBase(parent) {}

    QString id() const override { return QStringLiteral("curve"); }
    QString name() const override { return tr("Curve"); }
    QString toolTip() const override
    {
        return tr("Click to add control points, drag to bend the curve, double click to finish");
    }
    QString shortcutString() const override { return QStringLiteral("Shift+C"); }
    int sortOrder() const override { return 204; }

    QString statusText() const override
    {
        if (m_nodes.isEmpty())
            return tr("Click to place the first point");
        return tr("%1 points - double click or Enter to finish").arg(m_nodes.size());
    }

    QWidget* createOptionsWidget(QWidget* parent) override
    {
        buildOptions(parent, false);
        return m_optionsWidget;
    }

    void activate() override
    {
        cancelSession();
        ShapeToolBase::activate();
    }

    void deactivate() override
    {
        finish();
        ShapeToolBase::deactivate();
    }

    void mouseDown(const QPoint& docPos, Qt::MouseButton button, Qt::KeyboardModifiers) override
    {
        if (!document())
            return;
        m_button = button;
        // Grab an existing node to pull its tangent.
        for (int i = 0; i < m_nodes.size(); ++i) {
            if ((docPos - m_nodes[i].anchor).manhattanLength() <= 5) {
                m_dragNode = i;
                m_nodes[i].tangent = QPointF(0, 0);
                return;
            }
        }
        m_dragNode = -1;
        m_nodes.append({ QPointF(docPos), QPointF(0, 0) });
        m_dragNode = m_nodes.size() - 1;
        emit sessionChanged(true);
        requestOverlayUpdate();
        refreshStatus();
    }

    void mouseMove(const QPoint& docPos, Qt::MouseButtons, Qt::KeyboardModifiers) override
    {
        if (m_dragNode < 0 || m_dragNode >= m_nodes.size())
            return;
        m_nodes[m_dragNode].tangent = QPointF(docPos) - m_nodes[m_dragNode].anchor;
        requestOverlayUpdate();
    }

    void mouseUp(const QPoint& docPos, Qt::MouseButton, Qt::KeyboardModifiers) override
    {
        if (m_dragNode < 0)
            return;
        m_dragNode = -1;
        m_cur = docPos;
        if (m_nodes.size() < 2) {
            requestOverlayUpdate();
            return;
        }
        renderCurve();
    }

    void mouseDoubleClick(const QPoint&, Qt::MouseButton, Qt::KeyboardModifiers) override
    {
        finish();
    }

    void keyPressEvent(QKeyEvent* e) override
    {
        if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) {
            finish();
            e->accept();
            return;
        }
        if (e->key() == Qt::Key_Escape) {
            cancelSession();
            e->accept();
            return;
        }
        if (e->key() == Qt::Key_Backspace && !m_nodes.isEmpty()) {
            m_nodes.removeLast();
            requestOverlayUpdate();
            e->accept();
            return;
        }
        ShapeToolBase::keyPressEvent(e);
    }

    void drawOverlay(QPainter& p) override
    {
        if (m_nodes.isEmpty())
            return;
        const QPainterPath path = buildPath();
        drawShapePreview(p, path);
        p.save();
        QPen pen(QColor(0, 0, 0, 200));
        pen.setCosmetic(true);
        p.setPen(pen);
        p.setBrush(toQColor(fillColor()));
        for (const Node& n : m_nodes) {
            p.drawLine(n.anchor, n.anchor + n.tangent);
            p.drawLine(n.anchor, n.anchor - n.tangent);
            p.drawRect(QRectF(n.anchor.x() - 2, n.anchor.y() - 2, 4, 4));
        }
        p.restore();
    }

private:
    struct Node {
        QPointF anchor;
        QPointF tangent;
    };

    QPainterPath buildPath() const
    {
        QPainterPath path;
        if (m_nodes.size() < 2)
            return path;
        path.moveTo(m_nodes.first().anchor);
        for (int i = 0; i + 1 < m_nodes.size(); ++i) {
            const Node& a = m_nodes[i];
            const Node& b = m_nodes[i + 1];
            path.cubicTo(a.anchor + a.tangent, b.anchor - b.tangent, b.anchor);
        }
        return path;
    }

    /// Rasterises the curve (possibly several times, as nodes are added) into
    /// a stroke that is only committed by finish().
    void renderCurve()
    {
        if (!document() || !canPaint())
            return;
        Layer* l = activeLayer();
        if (!l)
            return;
        const QPainterPath path = buildPath();
        const QRect area = path.boundingRect().toRect().adjusted(-m_width - 2, -m_width - 2, m_width + 2,
                                                                 m_width + 2);
        if (area.isEmpty())
            return;
        if (!m_session) {
            m_buffer.begin(&l->surface());
            m_layerIndex = document()->activeLayerIndex();
            m_session = true;
        }
        m_buffer.touch(area);
        if (doFill())
            ImageMath::fillPath(l->surface(), path, fillColor(), clipSelection(), 1.0);
        if (doOutline())
            ImageMath::strokePath(l->surface(), path, lineColor(), clipSelection(), 1.0, m_width, true);
        m_dirty = m_dirty.isNull() ? area : m_dirty.united(area);
        l->markThumbnailDirty();
        document()->notifyLayerPixels(m_layerIndex, area);
        requestUpdate(area);
        requestOverlayUpdate();
    }

    bool finishSession() override { finish(); return true; }

    void finish()
    {
        if (m_session && document() && !m_dirty.isEmpty()) {
            Layer* l = document()->layerAt(m_layerIndex);
            if (l) {
                document()->notifyLayerPixels(m_layerIndex, m_dirty);
                pushShapeAction(this, name(), m_layerIndex, m_dirty, m_buffer.before(),
                                l->surface().cropped(m_dirty));
            }
        }
        emit sessionChanged(false);
        reset();
        refreshStatus();
    }

    /// Restores the pixels captured by the stroke buffer (nothing is pushed
    /// into the history).
    void cancelSession()
    {
        if (m_session && document() && !m_dirty.isEmpty()) {
            Layer* l = document()->layerAt(m_layerIndex);
            if (l) {
                const QImage img = m_buffer.before().toQImageConst();
                for (int y = m_dirty.top(); y <= m_dirty.bottom() && y < l->height(); ++y) {
                    const int sy = y - m_dirty.top();
                    if (sy < 0 || sy >= img.height())
                        continue;
                    pixel_t* d = l->surface().scanLine(y);
                    const pixel_t* s = reinterpret_cast<const pixel_t*>(img.constScanLine(sy));
                    for (int x = m_dirty.left(); x <= m_dirty.right() && x < l->width(); ++x)
                        d[x] = s[x - m_dirty.left()];
                }
                l->markThumbnailDirty();
                document()->notifyLayerPixels(m_layerIndex, m_dirty);
                requestUpdate(m_dirty);
            }
        }
        reset();
    }

    void reset()
    {
        m_session = false;
        m_nodes.clear();
        m_dragNode = -1;
        m_buffer.begin(nullptr);
        m_dirty = QRect();
        requestOverlayUpdate();
    }

    QVector<Node> m_nodes;
    int m_dragNode = -1;
    bool m_session = false;
    int m_layerIndex = -1;
    StrokeBuffer m_buffer;
    QRect m_dirty;
};

// ------------------------------------------------------------ freeform

class FreeformShapeTool : public ShapeToolBase
{
public:
    explicit FreeformShapeTool(QObject* parent) : ShapeToolBase(parent) {}

    QString id() const override { return QStringLiteral("freeform"); }
    QString name() const override { return tr("Freeform Shape"); }
    QString toolTip() const override
    {
        return tr("Draw a free-hand shape that is filled when the button is released");
    }
    QString shortcutString() const override { return QStringLiteral("Shift+A"); }
    int sortOrder() const override { return 205; }

    QString statusText() const override
    {
        if (!m_dragging)
            return QString();
        return tr("%1 points").arg(m_points.size());
    }

    QWidget* createOptionsWidget(QWidget* parent) override
    {
        buildOptions(parent, false);
        m_closedBox = qobject_cast<QCheckBox*>(ToolOptions::addCheck(m_layout, tr("Closed"), true));
        connect(m_closedBox, &QCheckBox::toggled, this, [this](bool v) {
            m_closed = v;
            requestOverlayUpdate();
        });
        return m_optionsWidget;
    }

    void activate() override
    {
        m_points.clear();
        ShapeToolBase::activate();
    }

    void deactivate() override
    {
        m_points.clear();
        requestOverlayUpdate();
        ShapeToolBase::deactivate();
    }

    void mouseDown(const QPoint& docPos, Qt::MouseButton button, Qt::KeyboardModifiers) override
    {
        if (!document())
            return;
        beginDrag(docPos, button);
        m_points.clear();
        m_points.append(docPos);
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
        m_cur = docPos;
        if ((docPos - m_points.last()).manhattanLength() >= 1)
            m_points.append(docPos);
        m_dragging = false;
        if (m_points.size() < 2) {
            m_points.clear();
            return;
        }
        QPainterPath path;
        path.moveTo(m_points.first());
        for (int i = 1; i < m_points.size(); ++i)
            path.lineTo(m_points[i]);
        if (m_closed)
            path.closeSubpath();
        m_points.clear();
        requestOverlayUpdate();
        if (!m_closed && doFill()) {
            // An open path can only be stroked.
            const int saved = m_mode;
            m_mode = Outline;
            paintShape(path, name());
            m_mode = saved;
            return;
        }
        paintShape(path, name());
    }

    void drawOverlay(QPainter& p) override
    {
        if (m_points.size() < 2)
            return;
        QPainterPath path;
        path.moveTo(m_points.first());
        for (int i = 1; i < m_points.size(); ++i)
            path.lineTo(m_points[i]);
        if (m_closed)
            path.closeSubpath();
        const bool fill = doFill() && m_closed;
        const bool outline = doOutline() || !m_closed;
        p.save();
        p.setRenderHint(QPainter::Antialiasing, true);
        if (fill) {
            QColor c = toQColor(fillColor());
            c.setAlpha(90);
            p.setPen(Qt::NoPen);
            p.setBrush(c);
            p.drawPath(path);
        }
        if (outline) {
            QPen pen(toQColor(lineColor()));
            pen.setCosmetic(true);
            p.setPen(pen);
            p.setBrush(Qt::NoBrush);
            p.drawPath(path);
        }
        p.restore();
    }

private:
    QVector<QPoint> m_points;
    bool m_closed = true;
    QCheckBox* m_closedBox = nullptr;
};

} // namespace

// ------------------------------------------------------------------ factory

QList<Tool*> createShapeTools()
{
    return { new LineTool(nullptr),      new RectangleTool(nullptr), new EllipseTool(nullptr),
             new PolygonTool(nullptr),   new CurveTool(nullptr),     new FreeformShapeTool(nullptr) };
}

} // namespace pnq
