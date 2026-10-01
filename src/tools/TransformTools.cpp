// Transform tools: rotate, free transform and flip.
//
// Like "Move Selected" these tools work on a snapshot: every mouse-move
// restores the captured pixels and re-blits the transformed image, so the
// result is committed as a single undo step on mouse-up.

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
#include <QLineF>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QSpinBox>
#include <QTransform>
#include <QBoxLayout>

#include <cmath>

namespace pnq {
namespace {

// ------------------------------------------------------------------ helpers

const double kRad2Deg = 180.0 / 3.14159265358979323846;

enum Interpolation { Nearest = 0, Bilinear, Bicubic };

/// Source-over pixel copy of `src` (top-left anchored at `dstPos`), optionally
/// restricted by an 8 bit mask expressed in src coordinates.
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

QPen cosmeticPen(const QColor& c, bool dashed = true)
{
    QPen pen(c);
    pen.setCosmetic(true);
    if (dashed) {
        pen.setStyle(Qt::DashLine);
        pen.setDashPattern({ 4.0, 3.0 });
    }
    return pen;
}

/// Renders `src` through `t` into a new image of the same size.
QImage renderTransformed(const QImage& src, const QTransform& t, bool smooth)
{
    if (src.isNull())
        return QImage();
    QImage out(src.size(), QImage::Format_ARGB32_Premultiplied);
    out.fill(Qt::transparent);
    QPainter p(&out);
    p.setRenderHint(QPainter::Antialiasing, smooth);
    p.setRenderHint(QPainter::SmoothPixmapTransform, smooth);
    p.setTransform(t);
    p.drawImage(QPointF(0, 0), src);
    p.end();
    return out;
}

// -------------------------------------------------------------- the session

/// Captures the pixels of a work area, lets the tool blit transformed
/// versions of them and finally commits (or drops) the whole operation.
class TransformSession
{
public:
    bool begin(Tool* tool, Document* doc, const QRect& workRect, const QString& name)
    {
        reset();
        if (!tool || !doc)
            return false;
        Layer* l = doc->activeLayer();
        if (!l)
            return false;
        m_tool = tool;
        m_doc = doc;
        m_rect = workRect;
        m_name = name;
        m_layerIndex = doc->activeLayerIndex();
        m_before = l->surface().cropped(workRect);
        m_snap = m_before.copy();
        m_mask = QImage();
        if (doc->hasSelection()) {
            Selection local(workRect.width(), workRect.height());
            local.setMask(doc->selection().mask().copy(workRect));
            Renderer::applyMask(m_snap, local, true);
            m_mask = local.mask();
        }
        m_valid = true;
        return true;
    }

    bool valid() const { return m_valid; }
    const QRect& rect() const { return m_rect; }
    QImage snapshot() const { return m_snap.toQImageConst(); }
    bool hasSelection() const { return !m_mask.isNull(); }

    /// Puts the captured pixels back.
    void restore()
    {
        Layer* l = layer();
        if (!l)
            return;
        blitRegion(l->surface(), m_rect.topLeft(), m_before.toQImageConst(), false, nullptr);
        notify();
    }

    /// Writes an already transformed image (same size as the snapshot) over
    /// the work area. Inside the selection the pixels are *replaced* (a scaled
    /// down image must not leave the original behind), outside it the original
    /// content is kept. Feathered selections are blended.
    void blit(const QImage& piece)
    {
        Layer* l = layer();
        if (!l || piece.isNull())
            return;
        Surface& s = l->surface();
        const QRect clip = m_rect.intersected(l->bounds());
        for (int y = clip.top(); y <= clip.bottom(); ++y) {
            const int ly = y - m_rect.top();
            if (ly < 0 || ly >= piece.height())
                continue;
            pixel_t* d = s.scanLine(y);
            const pixel_t* src = reinterpret_cast<const pixel_t*>(piece.constScanLine(ly));
            const uchar* m = m_mask.isNull() ? nullptr : m_mask.constScanLine(ly);
            for (int x = clip.left(); x <= clip.right(); ++x) {
                const int lx = x - m_rect.left();
                if (lx < 0 || lx >= piece.width())
                    continue;
                const int v = m ? m[lx] : 255;
                if (v == 0)
                    continue;
                pixel_t sp = src[lx];
                if (v >= 255) {
                    d[x] = sp;
                    continue;
                }
                sp = qPremult(quint8(getA(sp) * v / 255), quint8(getR(sp) * v / 255),
                              quint8(getG(sp) * v / 255), quint8(getB(sp) * v / 255));
                d[x] = composePixel(d[x], sp, BlendMode::Normal);
            }
        }
        notify();
    }

    void notify()
    {
        if (!m_doc)
            return;
        if (Layer* l = layer())
            l->markThumbnailDirty();
        m_doc->notifyLayerPixels(m_layerIndex, m_rect);
        if (m_tool) {
            m_tool->requestUpdate(m_rect);
            m_tool->requestOverlayUpdate();
        }
    }

    void commit()
    {
        if (!m_valid)
            return;
        Layer* l = layer();
        if (l && m_tool) {
            // Wrapped in a macro: PixelDeltaAction::isMergeable() is true, so
            // consecutive transforms would otherwise collapse into one step.
            Document* doc = m_doc;
            if (doc && doc->history() && !m_rect.isEmpty()) {
                MacroAction* macro = new MacroAction(m_name);
                macro->add(new PixelDeltaAction(m_name, m_layerIndex, m_rect, m_before,
                                                l->surface().cropped(m_rect)));
                doc->history()->push(macro);
            }
        }
        reset();
    }

    void abort()
    {
        if (m_valid)
            restore();
        reset();
    }

    void reset()
    {
        m_valid = false;
        m_tool = nullptr;
        m_doc = nullptr;
        m_rect = QRect();
        m_before = Surface();
        m_snap = Surface();
        m_mask = QImage();
        m_layerIndex = -1;
    }

private:
    Layer* layer() const { return m_doc ? m_doc->layerAt(m_layerIndex) : nullptr; }

    Tool* m_tool = nullptr;
    Document* m_doc = nullptr;
    QRect m_rect;
    QString m_name;
    int m_layerIndex = -1;
    Surface m_before;
    Surface m_snap;
    QImage m_mask;
    bool m_valid = false;
};

// -------------------------------------------------------------- base class

class TransformToolBase : public Tool
{
public:
    explicit TransformToolBase(QObject* parent) : Tool(parent) {}

    void activate() override
    {
        m_dragging = false;
        Tool::activate();
    }

    void deactivate() override
    {
        if (m_session.valid())
            m_session.abort();
        m_dragging = false;
        Tool::deactivate();
    }

protected:
    /// The area that is transformed: the selection when there is one,
    /// otherwise the whole layer.
    bool computeWorkRect(QRect& out) const
    {
        if (!document() || !canPaint())
            return false;
        Layer* l = activeLayer();
        if (!l)
            return false;
        const QRect base = document()->hasSelection() ? document()->selection().nonEmptyRect()
                                                      : l->bounds();
        if (base.isEmpty())
            return false;
        out = base.intersected(l->bounds());
        return !out.isEmpty();
    }

    bool beginSession(const QRect& workRect, const QString& name)
    {
        const bool ok = m_session.begin(this, document(), workRect, name);
        if (ok)
            m_workRect = workRect;
        return ok;
    }

    /// Applies a local-space transform to the snapshot and shows the result.
    void renderTransform(const QTransform& t, bool smooth)
    {
        if (!m_session.valid())
            return;
        m_session.restore();
        m_session.blit(renderTransformed(m_session.snapshot(), t, smooth));
    }

    QRect m_workRect;
    QPoint m_press;
    QPoint m_cur;
    bool m_dragging = false;
    TransformSession m_session;
};

// ------------------------------------------------------------------ rotate

class RotateTool : public TransformToolBase
{
public:
    explicit RotateTool(QObject* parent) : TransformToolBase(parent) {}

    QString id() const override { return QStringLiteral("rotate"); }
    QString name() const override { return tr("Rotate"); }
    QString toolTip() const override
    {
        return tr("Click to set the centre of rotation, then drag to rotate");
    }
    QString shortcutString() const override { return QStringLiteral("Ctrl+R"); }
    int sortOrder() const override { return 300; }

    QString statusText() const override
    {
        if (!m_dragging)
            return QString();
        int a = int(std::fmod(m_angle, 360.0));
        if (a < 0)
            a += 360;
        return tr("Angle: %1").arg(a);
    }

    QWidget* createOptionsWidget(QWidget* parent) override
    {
        QBoxLayout* layout = nullptr;
        m_optionsWidget = ToolOptions::createRoot(parent, &layout);
        m_angleBox = qobject_cast<QSpinBox*>(
            ToolOptions::addSpin(layout, tr("Angle"), -360, 360, m_angle, tr("°")));
        connect(m_angleBox, &QSpinBox::valueChanged, this, [this](int v) {
            m_angle = v;
            requestOverlayUpdate();
            refreshStatus();
        });
        m_maintainBox =
            qobject_cast<QCheckBox*>(ToolOptions::addCheck(layout, tr("Maintain size"), true));
        connect(m_maintainBox, &QCheckBox::toggled, this, [this](bool v) {
            m_maintainSize = v;
            requestOverlayUpdate();
        });
        return m_optionsWidget;
    }

    void mouseDown(const QPoint& docPos, Qt::MouseButton button, Qt::KeyboardModifiers) override
    {
        if (button == Qt::RightButton)
            return;
        QRect base;
        if (!computeWorkRect(base))
            return;
        // Without "maintain size" the corners of a rotated shape may stick out
        // of the original area, so the work area is enlarged to the largest
        // possible rotated bounding box.
        if (!m_maintainSize) {
            const int side = int(std::ceil((base.width() + base.height()) / std::sqrt(2.0))) + 2;
            const QPoint c = base.center();
            const QRect big(c.x() - side / 2, c.y() - side / 2, side, side);
            base = big.intersected(activeLayer()->bounds());
            if (base.isEmpty())
                return;
        }
        if (!beginSession(base, tr("Rotate")))
            return;
        m_center = docPos;
        m_press = docPos;
        m_cur = docPos;
        m_dragging = true;
        m_baseAngle = m_angle;
        renderTransform(rotation(), smooth());
        requestOverlayUpdate();
    }

    void mouseMove(const QPoint& docPos, Qt::MouseButtons, Qt::KeyboardModifiers mods) override
    {
        if (!m_dragging) {
            m_cur = docPos;
            return;
        }
        updateAngle(docPos, mods);
    }

    void mouseUp(const QPoint& docPos, Qt::MouseButton, Qt::KeyboardModifiers mods) override
    {
        if (!m_dragging)
            return;
        updateAngle(docPos, mods);
        m_dragging = false;
        if (qFuzzyIsNull(m_angle) || !m_session.valid()) {
            m_session.abort();
            m_angle = m_baseAngle;
            requestOverlayUpdate();
            return;
        }
        m_session.commit();
        requestOverlayUpdate();
    }

    void keyPressEvent(QKeyEvent* e) override
    {
        if (e->key() == Qt::Key_Escape) {
            if (m_dragging) {
                m_dragging = false;
                m_session.abort();
                m_angle = m_baseAngle;
                requestOverlayUpdate();
            }
            e->accept();
            return;
        }
        TransformToolBase::keyPressEvent(e);
    }

    void drawOverlay(QPainter& p) override
    {
        if (!m_dragging)
            return;
        const QColor c = toQColor(primaryColor());
        const double radius = QLineF(QPointF(m_center), QPointF(m_cur)).length();
        p.save();
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setPen(cosmeticPen(c));
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(QPointF(m_center), radius, radius);
        QPen solid = cosmeticPen(c, false);
        solid.setWidthF(1.0);
        p.setPen(solid);
        p.drawLine(QPointF(m_center), QPointF(m_cur));
        p.setBrush(c);
        p.setPen(Qt::NoPen);
        p.drawEllipse(QPointF(m_center), 2.0, 2.0);
        p.restore();
    }

private:
    bool smooth() const { return m_interpolation != Nearest; }

    /// Recomputes the angle from the pointer and repaints the preview.
    void updateAngle(const QPoint& docPos, Qt::KeyboardModifiers mods)
    {
        m_cur = docPos;
        const double delta = angleAt(docPos) - angleAt(m_press);
        double a = m_baseAngle + delta;
        if (mods & Qt::ShiftModifier)
            a = std::round(a / 15.0) * 15.0;
        if (std::abs(a - m_angle) > 0.01) {
            m_angle = a;
            renderTransform(rotation(), smooth());
        }
        requestOverlayUpdate();
        refreshStatus();
    }

    double angleAt(const QPoint& p) const
    {
        return std::atan2(p.y() - m_center.y(), p.x() - m_center.x()) * kRad2Deg;
    }

    QTransform rotation() const
    {
        const QPointF local(m_center.x() - m_workRect.left() + 0.0, m_center.y() - m_workRect.top() + 0.0);
        QTransform t;
        t.translate(local.x(), local.y());
        t.rotate(m_angle);
        t.translate(-local.x(), -local.y());
        return t;
    }

    QPoint m_center;
    double m_angle = 0.0;
    double m_baseAngle = 0.0;
    int m_interpolation = Bilinear;
    bool m_maintainSize = true;
    QSpinBox* m_angleBox = nullptr;
    QCheckBox* m_maintainBox = nullptr;
};

// ---------------------------------------------------------- free transform

class FreeTransformTool : public TransformToolBase
{
public:
    explicit FreeTransformTool(QObject* parent) : TransformToolBase(parent) {}

    QString id() const override { return QStringLiteral("transform"); }
    QString name() const override { return tr("Free Transform"); }
    QString toolTip() const override
    {
        return tr("Drag the handles of the frame to scale or move the contents");
    }
    QString shortcutString() const override { return QStringLiteral("Ctrl+T"); }
    int sortOrder() const override { return 301; }

    QString statusText() const override
    {
        if (m_rect.isEmpty())
            return tr("No transformable area");
        return tr("%1 x %2 at %3, %4")
            .arg(m_rect.width())
            .arg(m_rect.height())
            .arg(m_rect.left())
            .arg(m_rect.top());
    }

    QWidget* createOptionsWidget(QWidget* parent) override
    {
        QBoxLayout* layout = nullptr;
        m_optionsWidget = ToolOptions::createRoot(parent, &layout);
        m_aspectBox =
            qobject_cast<QCheckBox*>(ToolOptions::addCheck(layout, tr("Maintain aspect ratio"), true));
        connect(m_aspectBox, &QCheckBox::toggled, this, [this](bool v) { m_maintainAspect = v; });
        m_interpBox = qobject_cast<QComboBox*>(ToolOptions::addCombo(
            layout, tr("Interpolation"),
            { tr("Nearest neighbor"), tr("Bilinear"), tr("Bicubic") }, int(m_interpolation)));
        connect(m_interpBox, &QComboBox::currentIndexChanged, this, [this](int i) {
            m_interpolation = i;
        });
        return m_optionsWidget;
    }

    void activate() override
    {
        syncRect();
        TransformToolBase::activate();
    }

    void deactivate() override
    {
        syncRect();
        TransformToolBase::deactivate();
    }

    void mouseDown(const QPoint& docPos, Qt::MouseButton button, Qt::KeyboardModifiers) override
    {
        if (button == Qt::RightButton) {
            // Right click restores the original frame.
            if (m_session.valid())
                m_session.abort();
            syncRect();
            requestOverlayUpdate();
            return;
        }
        if (m_rect.isEmpty())
            syncRect();
        if (m_rect.isEmpty())
            return;
        int handle = handleAt(docPos);
        if (handle < 0 && !m_rect.contains(docPos))
            return;
        if (!beginSession(m_rect, tr("Free Transform")))
            return;
        m_handle = handle;
        m_press = docPos;
        m_cur = docPos;
        m_rectAtPress = m_rect;
        m_dragging = true;
        requestOverlayUpdate();
    }

    void mouseMove(const QPoint& docPos, Qt::MouseButtons, Qt::KeyboardModifiers) override
    {
        if (!m_dragging)
            return;
        updateFrame(docPos);
    }

    void mouseUp(const QPoint& docPos, Qt::MouseButton, Qt::KeyboardModifiers) override
    {
        if (!m_dragging)
            return;
        updateFrame(docPos);
        m_dragging = false;
        m_handle = -1;
        if (!m_session.valid())
            return;
        if (m_rect == m_rectAtPress) {
            m_session.abort();
            return;
        }
        m_session.commit();
        requestOverlayUpdate();
    }

    void keyPressEvent(QKeyEvent* e) override
    {
        if (e->key() == Qt::Key_Escape) {
            if (m_dragging && m_session.valid())
                m_session.abort();
            m_dragging = false;
            m_handle = -1;
            syncRect();
            requestOverlayUpdate();
            e->accept();
            return;
        }
        TransformToolBase::keyPressEvent(e);
    }

    void drawOverlay(QPainter& p) override
    {
        if (m_rect.isEmpty())
            return;
        const QColor c = toQColor(primaryColor());
        p.save();
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setPen(cosmeticPen(c));
        p.setBrush(Qt::NoBrush);
        p.drawRect(QRectF(m_rect));
        p.setPen(QPen(QColor(0, 0, 0, 180)));
        p.setBrush(c);
        for (const QPoint& h : handlePoints())
            p.drawRect(QRectF(h.x() - 3, h.y() - 3, 6, 6));
        p.restore();
    }

private:
    /// Recomputes the destination frame and repaints the transformed content.
    void updateFrame(const QPoint& docPos)
    {
        m_cur = docPos;
        const QRect target = draggedRect(docPos);
        if (target.isEmpty() || target == m_rectAtPress)
            return;
        const double sx = double(target.width()) / double(qMax(1, m_rectAtPress.width()));
        const double sy = double(target.height()) / double(qMax(1, m_rectAtPress.height()));
        QTransform t;
        t.translate(target.left() - m_rectAtPress.left(), target.top() - m_rectAtPress.top());
        t.scale(sx, sy);
        renderTransform(t, m_interpolation != Nearest);
        m_rect = target;
        requestOverlayUpdate();
        refreshStatus();
    }

    static const QVector<QPointF>& handleOffsets()
    {
        static const QVector<QPointF> offs = {
            QPointF(0, 0),   QPointF(1, 0),   QPointF(1, 1),   QPointF(0, 1),
            QPointF(0.5, 0), QPointF(1, 0.5), QPointF(0.5, 1), QPointF(0, 0.5)
        };
        return offs;
    }

    QVector<QPoint> handlePoints() const
    {
        QVector<QPoint> pts;
        const QRectF r(m_rect);
        for (const QPointF& o : handleOffsets())
            pts.append((r.topLeft() + QPointF(o.x() * r.width(), o.y() * r.height())).toPoint());
        return pts;
    }

    int handleAt(const QPoint& pos) const
    {
        const double tol = qMax(4.0, 7.0 / (view() ? view()->zoom() : 1.0));
        const QVector<QPoint> pts = handlePoints();
        for (int i = 0; i < pts.size(); ++i) {
            if (QLineF(QPointF(pts[i]), QPointF(pos)).length() <= tol)
                return i;
        }
        return -1;
    }

    /// The frame the content is transformed into while dragging.
    QRect draggedRect(const QPoint& pos) const
    {
        const QRect orig = m_rectAtPress;
        if (m_handle < 0) {
            // Inside the frame -> move it as a whole.
            QRect r = orig;
            r.translate(pos - m_press);
            return r;
        }
        if (m_handle <= 3)
            return cornerRect(orig, m_handle, pos);
        QRect r = orig;
        switch (m_handle) {
        case 4:  // top edge
            r.setTop(qMin(pos.y(), r.bottom()));
            break;
        case 5:  // right edge
            r.setRight(qMax(pos.x(), r.left()));
            break;
        case 6:  // bottom edge
            r.setBottom(qMax(pos.y(), r.top()));
            break;
        default:  // left edge
            r.setLeft(qMin(pos.x(), r.right()));
            break;
        }
        if (r.width() < 1)
            r.setWidth(1);
        if (r.height() < 1)
            r.setHeight(1);
        return r;
    }

    /// Corner handles: the opposite corner stays anchored and the aspect ratio
    /// is preserved when requested.
    QRect cornerRect(const QRect& orig, int handle, const QPoint& pos) const
    {
        QPoint anchor;
        switch (handle) {
        case 0:
            anchor = orig.bottomRight();
            break;
        case 1:
            anchor = orig.bottomLeft();
            break;
        case 2:
            anchor = orig.topLeft();
            break;
        default:
            anchor = orig.topRight();
            break;
        }
        int w = qAbs(pos.x() - anchor.x());
        int h = qAbs(pos.y() - anchor.y());
        if (m_maintainAspect && orig.width() > 0 && orig.height() > 0) {
            if (double(w) * orig.height() > double(h) * orig.width())
                h = qMax(1, int(double(w) * orig.height() / orig.width()));
            else
                w = qMax(1, int(double(h) * orig.width() / orig.height()));
        }
        w = qMax(1, w);
        h = qMax(1, h);
        return QRect(qMin(anchor.x(), pos.x()), qMin(anchor.y(), pos.y()), w, h);
    }

    void syncRect()
    {
        QRect r;
        if (computeWorkRect(r))
            m_rect = r;
    }

    QRect m_rect;
    QRect m_rectAtPress;
    int m_handle = -1;
    int m_interpolation = Bilinear;
    bool m_maintainAspect = true;
    QCheckBox* m_aspectBox = nullptr;
    QComboBox* m_interpBox = nullptr;
};

// -------------------------------------------------------------------- flip

class FlipTool : public TransformToolBase
{
public:
    explicit FlipTool(QObject* parent) : TransformToolBase(parent) {}

    QString id() const override { return QStringLiteral("flip"); }
    QString name() const override { return tr("Flip"); }
    QString toolTip() const override
    {
        return tr("Flip the selection (or the layer) horizontally or vertically");
    }
    QString shortcutString() const override { return QStringLiteral("Ctrl+F"); }
    int sortOrder() const override { return 302; }

    QString statusText() const override
    {
        return m_horizontal ? tr("Flip horizontal") : tr("Flip vertical");
    }

    QWidget* createOptionsWidget(QWidget* parent) override
    {
        QBoxLayout* layout = nullptr;
        m_optionsWidget = ToolOptions::createRoot(parent, &layout);
        m_axisBox = qobject_cast<QComboBox*>(ToolOptions::addCombo(
            layout, tr("Axis"), { tr("Horizontal"), tr("Vertical") }, 0));
        connect(m_axisBox, &QComboBox::currentIndexChanged, this, [this](int i) {
            m_horizontal = (i == 0);
            refreshStatus();
        });
        return m_optionsWidget;
    }

    void mouseDown(const QPoint& docPos, Qt::MouseButton, Qt::KeyboardModifiers) override
    {
        m_press = docPos;
        m_cur = docPos;
        m_dragging = true;
    }

    void mouseMove(const QPoint& docPos, Qt::MouseButtons, Qt::KeyboardModifiers) override
    {
        m_cur = docPos;
    }

    void mouseUp(const QPoint& docPos, Qt::MouseButton, Qt::KeyboardModifiers) override
    {
        m_dragging = false;
        QRect work;
        if (!computeWorkRect(work))
            return;
        // Dragging sideways flips horizontally, dragging up/down vertically.
        const QPoint d = docPos - m_press;
        bool horizontal = m_horizontal;
        if (qAbs(d.x()) > 4 || qAbs(d.y()) > 4)
            horizontal = qAbs(d.x()) >= qAbs(d.y());
        if (!beginSession(work, horizontal ? tr("Flip Horizontal") : tr("Flip Vertical")))
            return;
        m_session.restore();
        const QImage piece = m_session.snapshot().flipped(
            horizontal ? Qt::Orientations(Qt::Horizontal) : Qt::Orientations(Qt::Vertical));
        m_session.blit(piece);
        m_session.commit();
        requestOverlayUpdate();
    }

    void drawOverlay(QPainter&) override {}

private:
    bool m_horizontal = true;
    QComboBox* m_axisBox = nullptr;
};

} // namespace

// ------------------------------------------------------------------ factory

QList<Tool*> createTransformTools()
{
    return { new RotateTool(nullptr), new FreeTransformTool(nullptr), new FlipTool(nullptr) };
}

} // namespace pnq
