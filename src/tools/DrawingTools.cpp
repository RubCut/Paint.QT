// Freehand drawing tools: paintbrush, textured paintbrush, eraser, blur,
// smudge, dodge/burn and recolor.
//
// Every painting tool follows the same protocol:
//   mouseDown  -> remember position, start the StrokeBuffer, stamp once
//   mouseMove  -> touch() the affected rect, then draw
//   mouseUp    -> push a PixelDeltaAction built from the buffer
#include "tools/Tool.h"

#include "core/BlendMode.h"
#include "core/Brush.h"
#include "core/ColorUtils.h"
#include "core/Document.h"
#include "core/History.h"
#include "core/ImageMath.h"
#include "core/Layer.h"
#include "core/Selection.h"
#include "core/Surface.h"
#include "tools/ToolSupport.h"
#include "ui/CanvasView.h"
#include "ui/ToolOptions.h"

#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QSlider>
#include <QSpinBox>
#include <QBoxLayout>
#include <QWidget>
#include <cmath>

namespace pnq {

namespace {

// ------------------------------------------------------------------ helpers

/// Rectangle covered by a brush stamp centred at `p` (a little oversized so the
/// antialiased border is included).
QRect stampRect(const QPointF& p, int size)
{
    const int r = size / 2 + 1;
    const int cx = int(std::floor(p.x()));
    const int cy = int(std::floor(p.y()));
    return QRect(cx - r, cy - r, 2 * r + 1, 2 * r + 1);
}

/// Walks the stamp positions between a and b using the brush spacing.
template <typename Fn>
void forEachStamp(const QPointF& a, const QPointF& b, const Brush& br, Fn&& fn)
{
    const double step = qMax(1.0, br.spacingDistance());
    const double dist = std::hypot(b.x() - a.x(), b.y() - a.y());
    const int n = int(std::ceil(dist / step));
    for (int i = 0; i <= n; ++i) {
        const double t = n > 0 ? double(i) / double(n) : 0.0;
        fn(QPointF(a.x() + (b.x() - a.x()) * t, a.y() + (b.y() - a.y()) * t));
    }
}

/// Alpha coverage (0..255) of the brush stamped along a segment, expressed in
/// the coordinates of `area`. Used by the tools that recolour pixels instead of
/// painting a solid colour.
QImage stampCoverage(const QRect& area, const QPointF& a, const QPointF& b, const Brush& br)
{
    QImage cov(area.size(), QImage::Format_Alpha8);
    cov.fill(0);
    const QImage& st = br.stamp();
    const int d = st.width();
    if (d <= 0)
        return cov;
    forEachStamp(a, b, br, [&](const QPointF& p) {
        const int cx = int(std::floor(p.x() - d / 2.0 + 0.5)) - area.left();
        const int cy = int(std::floor(p.y() - d / 2.0 + 0.5)) - area.top();
        for (int y = 0; y < d; ++y) {
            const int yy = cy + y;
            if (yy < 0 || yy >= cov.height())
                continue;
            uchar* dst = cov.scanLine(yy);
            const uchar* srow = st.constScanLine(y);
            for (int x = 0; x < d; ++x) {
                const int xx = cx + x;
                if (xx < 0 || xx >= cov.width())
                    continue;
                if (srow[x] > dst[xx])
                    dst[xx] = srow[x];
            }
        }
    });
    return cov;
}

inline int selectionValue(const Selection* sel, int x, int y)
{
    return sel ? sel->at(x, y) : 255;
}

/// Interpolates two premultiplied pixels.
inline pixel_t lerpPixel(pixel_t a, pixel_t b, int f)
{
    auto mix = [f](quint8 x, quint8 y) {
        return quint8((int(x) * (255 - f) + int(y) * f) / 255);
    };
    return qPremult(mix(chanA(a), chanA(b)), mix(chanR(a), chanR(b)), mix(chanG(a), chanG(b)),
                    mix(chanB(a), chanB(b)));
}

/// Blend-mode names in the very order of allBlendModes() so that the combo
/// index maps straight onto the enum.
QStringList blendModeNames()
{
    QStringList names;
    const QList<BlendMode> modes = allBlendModes();
    names.reserve(modes.size());
    for (BlendMode m : modes)
        names << QString::fromLatin1(blendModeName(m));
    return names;
}

// ------------------------------------------------------------- stroke base

/// Shared mouse handling / undo bookkeeping for every freehand tool.
class StrokeToolBase : public Tool
{
public:
    explicit StrokeToolBase(QObject* parent) : Tool(parent) {}

    void mouseDown(const QPoint& docPos, Qt::MouseButton button, Qt::KeyboardModifiers mods) override
    {
        if (m_drawing)
            finishStroke();
        if (!canPaint() || !activeLayer()) {
            refreshStatus();
            return;
        }
        m_layerIndex = activeLayerIndex();
        m_buffer.begin(&activeLayer()->surface());
        m_drawing = true;
        m_button = button;
        m_color = colorForButton(button);
        m_last = QPointF(docPos);
        m_pos = m_last;
        m_hasPos = true;
        m_path.clear();
        m_path.append(m_last);
        m_emitted = m_last;
        m_hasEmitted = true;
        beginStroke(m_last, mods);
        paintSegment(m_last, m_last, mods);
        requestOverlayUpdate();
        refreshStatus();
    }

    void mouseMove(const QPoint& docPos, Qt::MouseButtons buttons, Qt::KeyboardModifiers mods) override
    {
        const QPointF p(docPos);
        m_pos = p;
        m_hasPos = true;
        if (m_drawing && (buttons & m_button)) {
            m_last = p;
            if (p != m_path.last()) {
                m_path.append(p);
                emitSmoothedPath(mods);
            }
        }
        requestOverlayUpdate();
        refreshStatus();
    }

    void mouseUp(const QPoint& docPos, Qt::MouseButton button, Qt::KeyboardModifiers mods) override
    {
        if (!m_drawing)
            return;
        const QPointF p(docPos);
        m_pos = p;
        m_hasPos = true;
        if (button == m_button) {
            if (p != m_path.last()) {
                m_path.append(p);
                emitSmoothedPath(mods);
            }
            // The final span is still pending because it needs a control point
            // that only arrives on the next move; draw it out to the release.
            if (m_hasEmitted && m_path.last() != m_emitted)
                paintSegment(m_emitted, m_path.last(), mods);
        }
        m_last = p;
        finishStroke();
        requestOverlayUpdate();
    }

    void deactivate() override
    {
        if (m_drawing)
            finishStroke();
        Tool::deactivate();
    }

    void drawOverlay(QPainter& p) override
    {
        if (!m_hasPos)
            return;
        const double z = m_view ? m_view->zoom() : 1.0;
        QPen pen(QColor(0, 0, 0, 170));
        pen.setWidthF(1.0 / qMax(0.01, z));
        p.setPen(pen);
        p.setBrush(Qt::NoBrush);
        const double r = brush().size() / 2.0;
        p.drawEllipse(m_pos, r, r);
    }

protected:
    virtual void beginStroke(const QPointF&, Qt::KeyboardModifiers) {}
    virtual void endStroke() {}
    virtual void paintSegment(const QPointF& a, const QPointF& b, Qt::KeyboardModifiers mods) = 0;

    void touchArea(const QRect& r) { m_buffer.touch(r); }

    /// Emits the part of the cursor path that has become final.
    ///
    /// Raw mouse events are sparse, so joining them with straight lines turns
    /// fast strokes into visible chords. Instead the path is treated as a
    /// Catmull-Rom spline through the samples and each settled span is emitted
    /// as a short curve. The last two samples stay pending until the next event
    /// arrives, which is where the control point for the next span comes from.
    void emitSmoothedPath(Qt::KeyboardModifiers mods)
    {
        const int n = m_path.size();
        if (n < 2 || !m_hasEmitted)
            return;
        if (n == 2) {
            paintSegment(m_emitted, m_path.at(1), mods);
            m_emitted = m_path.at(1);
            return;
        }
        // Span between sample n-3 and n-2, using n-4 .. n-1 as the four
        // Catmull-Rom control points. Clamp the ends against the path bounds.
        const QPointF p0 = m_path.at(qMax(0, n - 4));
        const QPointF p1 = m_path.at(n - 3);
        const QPointF p2 = m_path.at(n - 2);
        const QPointF p3 = m_path.at(n - 1);

        // Uniform Catmull-Rom converted to a cubic Bezier, sampled by arc length.
        const QPointF c1 = p1 + (p2 - p0) / 6.0;
        const QPointF c2 = p2 - (p3 - p1) / 6.0;
        const double chord = std::hypot(p2.x() - p1.x(), p2.y() - p1.y());
        const int steps = qBound(1, int(std::ceil(chord * 2.0)), 64);

        QPointF prev = p1;
        for (int i = 1; i <= steps; ++i) {
            const double t = double(i) / steps;
            const double u = 1.0 - t;
            const double w0 = u * u * u;
            const double w1 = 3 * u * u * t;
            const double w2 = 3 * u * t * t;
            const double w3 = t * t * t;
            const QPointF cur(p1.x() * w0 + c1.x() * w1 + c2.x() * w2 + p2.x() * w3,
                               p1.y() * w0 + c1.y() * w1 + c2.y() * w2 + p2.y() * w3);
            if (cur != prev) {
                paintSegment(prev, cur, mods);
                prev = cur;
            }
        }
        m_emitted = p2;
    }

    void finishStroke()
    {
        m_drawing = false;
        const QRect dirty = m_buffer.end();
        endStroke();
        if (dirty.isEmpty())
            return;
        pushStrokeAction(this, name(), m_layerIndex, m_buffer);
    }

    QRect areaFor(const QPointF& a, const QPointF& b, int size) const
    {
        return stampRect(a, size).united(stampRect(b, size));
    }

    /// Combines the stamp coverage with the brush opacity and the selection.
    int coverageAt(const QImage& cov, int x, int y, int localX, int localY, int opacity,
                   const Selection* sel) const
    {
        int a = cov.constScanLine(localY)[localX];
        if (!a)
            return 0;
        a = a * opacity / 100;
        a = a * selectionValue(sel, x, y) / 255;
        return a;
    }

    StrokeBuffer m_buffer;
    QPointF m_last;
    QPointF m_pos;
    /// Raw cursor samples for the current stroke, smoothed before painting.
    QList<QPointF> m_path;
    /// Last point already handed to paintSegment().
    QPointF m_emitted;
    bool m_hasEmitted = false;
    bool m_hasPos = false;
    bool m_drawing = false;
    Qt::MouseButton m_button = Qt::LeftButton;
    pixel_t m_color = 0xFF000000u;
    int m_layerIndex = -1;
};

// ------------------------------------------------------------ paint / pencil

/// Paintbrush (a.k.a. "pencil"): stamps the brush and can composite the result
/// with an arbitrary blend mode.
class PaintTool : public StrokeToolBase
{
public:
    explicit PaintTool(QObject* parent) : StrokeToolBase(parent) {}

    QWidget* createOptionsWidget(QWidget* parent) override
    {
        QBoxLayout* lay = nullptr;
        QWidget* root = ToolOptions::createRoot(parent, &lay);

        m_size = ToolOptions::addSlider(lay, tr("Size"), 1, 500, brush().size(), tr(" px"));
        connect(m_size.slider, &QSlider::valueChanged, this, [this](int v) {
            brush().setSize(v);
            emit optionsChanged();
        });

        m_hardness = ToolOptions::addSlider(lay, tr("Hardness"), 0, 100, brush().hardness(), QStringLiteral(" %"));
        connect(m_hardness.slider, &QSlider::valueChanged, this, [this](int v) {
            brush().setHardness(v);
            emit optionsChanged();
        });

        m_opacity = ToolOptions::addSlider(lay, tr("Opacity"), 1, 100, brush().opacity(), QStringLiteral(" %"));
        connect(m_opacity.slider, &QSlider::valueChanged, this, [this](int v) {
            brush().setOpacity(v);
            emit optionsChanged();
        });

        m_spacing = ToolOptions::addSlider(lay, tr("Spacing"), 1, 100, brush().spacing(), QStringLiteral(" %"));
        connect(m_spacing.slider, &QSlider::valueChanged, this, [this](int v) {
            brush().setSpacing(v);
            emit optionsChanged();
        });

        QCheckBox* aa = static_cast<QCheckBox*>(ToolOptions::addCheck(lay, tr("Anti-aliasing"), brush().antiAliasing()));
        connect(aa, &QCheckBox::toggled, this, [this](bool on) {
            brush().setAntiAliasing(on);
            emit optionsChanged();
        });

        m_blendCombo = static_cast<QComboBox*>(
            ToolOptions::addCombo(lay, tr("Blend mode"), blendModeNames(), 0));
        connect(m_blendCombo, &QComboBox::currentIndexChanged, this, [this](int idx) {
            const QList<BlendMode> modes = allBlendModes();
            if (idx >= 0 && idx < modes.size()) {
                m_blend = modes.at(idx);
                emit optionsChanged();
            }
        });

        addToolOptions(lay);

        m_optionsWidget = root;
        return root;
    }

    QString statusText() const override
    {
        return tr("Left button paints the primary colour, right button the secondary one.");
    }

protected:
    /// Hook for derived tools that add extra rows to the options bar.
    virtual void addToolOptions(QBoxLayout*) {}

    void paintSegment(const QPointF& a, const QPointF& b, Qt::KeyboardModifiers mods) override
    {
        Q_UNUSED(mods);
        Layer* layer = activeLayer();
        if (!layer)
            return;
        const QRect area = areaFor(a, b, brush().size()).intersected(layer->bounds());
        if (area.isEmpty())
            return;
        touchArea(area);

        if (m_blend == BlendMode::Normal) {
            brush().drawSegment(layer->surface(), a, b, m_color, selectionOrNull());
        } else {
            // Non normal blend modes: render the stamps into a scratch surface
            // and composite them onto the layer with the requested mode.
            Surface scratch(area.size());
            scratch.fill(transparentPixel());
            const Selection* sel = selectionOrNull();
            const bool hasSel = sel && !sel->isNull();
            Selection subSel;
            if (hasSel)
                subSel.setMask(sel->mask().copy(area));
            const QPointF off(area.topLeft());
            brush().drawSegment(scratch, a - off, b - off, m_color, hasSel ? &subSel : nullptr);

            for (int y = area.top(); y <= area.bottom(); ++y) {
                pixel_t* row = layer->surface().scanLine(y);
                const pixel_t* srow = scratch.scanLine(y - area.top());
                for (int x = area.left(); x <= area.right(); ++x) {
                    if (!layer->canPaintAt(x, y))
                        continue;
                    row[x] = composePixel(row[x], srow[x - area.left()], m_blend);
                }
            }
        }
        requestUpdate(area);
        requestOverlayUpdate();
    }

    ToolOptions::SliderRow m_size;
    ToolOptions::SliderRow m_hardness;
    ToolOptions::SliderRow m_opacity;
    ToolOptions::SliderRow m_spacing;
    QComboBox* m_blendCombo = nullptr;
    BlendMode m_blend = BlendMode::Normal;
};

class PencilTool : public PaintTool
{
public:
    explicit PencilTool(QObject* parent) : PaintTool(parent) {}

    QString id() const override { return QStringLiteral("pencil"); }
    QString name() const override { return tr("Paintbrush"); }
    QString toolTip() const override { return tr("Draw freehand strokes with the current brush"); }
    QString shortcutString() const override { return QStringLiteral("B"); }
    int sortOrder() const override { return 0; }
};

/// Paintbrush with a texture: same engine, extra texture picker.
class BrushTextureTool : public PaintTool
{
public:
    explicit BrushTextureTool(QObject* parent) : PaintTool(parent) {}

    QString id() const override { return QStringLiteral("brush"); }
    QString name() const override { return tr("Paintbrush (textured)"); }
    QString toolTip() const override { return tr("Freehand brush strokes modulated by a texture"); }
    QString shortcutString() const override { return QStringLiteral("Ctrl+B"); }
    int sortOrder() const override { return 1; }

protected:
    void addToolOptions(QBoxLayout* lay) override
    {
        const QStringList textures = Brush::defaultTextureNames();
        m_textureCombo = static_cast<QComboBox*>(
            ToolOptions::addCombo(lay, tr("Texture"), textures, 0));
        connect(m_textureCombo, &QComboBox::currentIndexChanged, this, [this](int idx) {
            const QStringList names = Brush::defaultTextureNames();
            if (idx < 0 || idx >= names.size())
                return;
            const QString name = names.at(idx);
            brush().setTexture(Brush::generateTexture(name));
            emit optionsChanged();
        });
    }

private:
    QComboBox* m_textureCombo = nullptr;
};

// ------------------------------------------------------------------- eraser

/// Removes the alpha of the pixels it covers (Paint.NET style eraser).
class EraserTool : public StrokeToolBase
{
public:
    explicit EraserTool(QObject* parent) : StrokeToolBase(parent) {}

    QString id() const override { return QStringLiteral("eraser"); }
    QString name() const override { return tr("Eraser"); }
    QString toolTip() const override { return tr("Erase the alpha of the active layer"); }
    QString shortcutString() const override { return QStringLiteral("S"); }
    int sortOrder() const override { return 2; }
    Qt::CursorShape cursorShape() const override { return Qt::CrossCursor; }

    QWidget* createOptionsWidget(QWidget* parent) override
    {
        QBoxLayout* lay = nullptr;
        QWidget* root = ToolOptions::createRoot(parent, &lay);

        m_size = ToolOptions::addSlider(lay, tr("Size"), 1, 500, brush().size(), tr(" px"));
        connect(m_size.slider, &QSlider::valueChanged, this, [this](int v) {
            brush().setSize(v);
            emit optionsChanged();
        });

        m_hardness = ToolOptions::addSlider(lay, tr("Hardness"), 0, 100, brush().hardness(), QStringLiteral(" %"));
        connect(m_hardness.slider, &QSlider::valueChanged, this, [this](int v) {
            brush().setHardness(v);
            emit optionsChanged();
        });

        m_spacing = ToolOptions::addSlider(lay, tr("Spacing"), 1, 100, brush().spacing(), QStringLiteral(" %"));
        connect(m_spacing.slider, &QSlider::valueChanged, this, [this](int v) {
            brush().setSpacing(v);
            emit optionsChanged();
        });

        m_optionsWidget = root;
        return root;
    }

    QString statusText() const override
    {
        return tr("Erases inside the selection only; locked layers are left alone.");
    }

protected:
    void beginStroke(const QPointF&, Qt::KeyboardModifiers) override
    {
        // The eraser always removes alpha, whatever brush the user picked.
        brush().setMode(BrushMode::Erase);
        brush().setTexture(QImage());
    }

    void paintSegment(const QPointF& a, const QPointF& b, Qt::KeyboardModifiers mods) override
    {
        Q_UNUSED(mods);
        Layer* layer = activeLayer();
        if (!layer)
            return;
        const QRect area = areaFor(a, b, brush().size()).intersected(layer->bounds());
        if (area.isEmpty())
            return;
        touchArea(area);
        brush().drawSegment(layer->surface(), a, b, whitePixel(), selectionOrNull());
        requestUpdate(area);
    }

private:
    ToolOptions::SliderRow m_size;
    ToolOptions::SliderRow m_hardness;
    ToolOptions::SliderRow m_spacing;
};

// --------------------------------------------------------------------- blur

class BlurTool : public StrokeToolBase
{
public:
    explicit BlurTool(QObject* parent) : StrokeToolBase(parent) {}

    QString id() const override { return QStringLiteral("blur"); }
    QString name() const override { return tr("Blur"); }
    QString toolTip() const override { return tr("Blur the layer under the brush"); }
    QString shortcutString() const override { return QStringLiteral("Shift+B"); }
    int sortOrder() const override { return 3; }

    QWidget* createOptionsWidget(QWidget* parent) override
    {
        QBoxLayout* lay = nullptr;
        QWidget* root = ToolOptions::createRoot(parent, &lay);

        m_size = ToolOptions::addSlider(lay, tr("Size"), 1, 500, brush().size(), tr(" px"));
        connect(m_size.slider, &QSlider::valueChanged, this, [this](int v) {
            brush().setSize(v);
            emit optionsChanged();
        });

        m_radius = ToolOptions::addSlider(lay, tr("Radius"), 0, 100, m_blurRadius, QStringLiteral(" px"));
        connect(m_radius.slider, &QSlider::valueChanged, this, [this](int v) {
            m_blurRadius = v;
            emit optionsChanged();
        });

        m_sampling = ToolOptions::addSlider(lay, tr("Sampling percent"), 1, 100, m_samplingPercent, QStringLiteral(" %"));
        connect(m_sampling.slider, &QSlider::valueChanged, this, [this](int v) {
            m_samplingPercent = v;
            emit optionsChanged();
        });

        m_optionsWidget = root;
        return root;
    }

    QString statusText() const override
    {
        return tr("Radius is the blur kernel size, sampling mixes the blurred and the original pixels.");
    }

protected:
    void paintSegment(const QPointF& a, const QPointF& b, Qt::KeyboardModifiers mods) override
    {
        Q_UNUSED(mods);
        Layer* layer = activeLayer();
        if (!layer)
            return;
        const int rad = qBound(0, m_blurRadius, 100);
        QRect area = areaFor(a, b, brush().size()).adjusted(-rad, -rad, rad, rad);
        area = area.intersected(layer->bounds());
        if (area.isEmpty())
            return;
        touchArea(area);

        Surface snap = layer->surface().cropped(area);
        if (rad > 0)
            ImageMath::boxBlur(snap, rad);

        const QImage cov = stampCoverage(area, b, b, brush());
        const int opacity = brush().opacity();
        const int sampling = qBound(1, m_samplingPercent, 100);
        const Selection* sel = selectionOrNull();
        Surface& target = layer->surface();
        for (int y = area.top(); y <= area.bottom(); ++y) {
            pixel_t* row = target.scanLine(y);
            const pixel_t* srow = snap.scanLine(y - area.top());
            for (int x = area.left(); x <= area.right(); ++x) {
                const int a = coverageAt(cov, x, y, x - area.left(), y - area.top(), opacity, sel);
                if (!a || !layer->canPaintAt(x, y))
                    continue;
                const pixel_t orig = row[x];
                const pixel_t blurred = srow[x - area.left()];
                const pixel_t mixed = sampling >= 100 ? blurred : lerpPixel(orig, blurred, sampling);
                row[x] = composePixel(row[x], qPremult(quint8(a), getR(mixed), getG(mixed), getB(mixed)),
                                      BlendMode::Normal);
            }
        }
        requestUpdate(area);
    }

private:
    ToolOptions::SliderRow m_size;
    ToolOptions::SliderRow m_radius;
    ToolOptions::SliderRow m_sampling;
    int m_blurRadius = 3;
    int m_samplingPercent = 100;
};

// ------------------------------------------------------------------- smudge

/// Drags the colours picked up along the stroke.
class SmudgeTool : public StrokeToolBase
{
public:
    explicit SmudgeTool(QObject* parent) : StrokeToolBase(parent) {}

    QString id() const override { return QStringLiteral("smudge"); }
    QString name() const override { return tr("Smudge"); }
    QString toolTip() const override { return tr("Smear the colours of the layer"); }
    QString shortcutString() const override { return QStringLiteral("Shift+S"); }
    int sortOrder() const override { return 4; }

    QWidget* createOptionsWidget(QWidget* parent) override
    {
        QBoxLayout* lay = nullptr;
        QWidget* root = ToolOptions::createRoot(parent, &lay);

        m_size = ToolOptions::addSlider(lay, tr("Size"), 1, 500, brush().size(), tr(" px"));
        connect(m_size.slider, &QSlider::valueChanged, this, [this](int v) {
            brush().setSize(v);
            emit optionsChanged();
        });

        m_strength = ToolOptions::addSlider(lay, tr("Strength"), 1, 100, m_smudgeStrength, QStringLiteral(" %"));
        connect(m_strength.slider, &QSlider::valueChanged, this, [this](int v) {
            m_smudgeStrength = v;
            emit optionsChanged();
        });

        m_optionsWidget = root;
        return root;
    }

    QString statusText() const override
    {
        return tr("Strength is how fast the brush picks up the colour it passes over.");
    }

protected:
    void beginStroke(const QPointF& p, Qt::KeyboardModifiers mods) override
    {
        Q_UNUSED(mods);
        m_haveCarry = false;
        m_carry = QImage();
        m_prevCenter = p;
    }

    void endStroke() override
    {
        m_haveCarry = false;
        m_carry = QImage();
    }

    void paintSegment(const QPointF& a, const QPointF& b, Qt::KeyboardModifiers mods) override
    {
        Q_UNUSED(mods);
        Layer* layer = activeLayer();
        if (!layer)
            return;
        const Brush& br = brush();
        const int d = br.size();
        const QImage& st = br.stamp();
        const int strength = qBound(1, m_smudgeStrength, 100);
        const int opacity = br.opacity();
        const Selection* sel = selectionOrNull();

        forEachStamp(a, b, br, [&](const QPointF& c) {
            const QPoint tl(int(std::floor(c.x() - d / 2.0 + 0.5)), int(std::floor(c.y() - d / 2.0 + 0.5)));
            const QRect area = QRect(tl, QSize(d, d));
            touchArea(area);

            // Sample the layer under the stamp.
            QImage cur(d, d, QImage::Format_ARGB32_Premultiplied);
            cur.fill(0);
            for (int y = 0; y < d; ++y) {
                const int dy = tl.y() + y;
                if (dy < 0 || dy >= layer->height())
                    continue;
                const pixel_t* row = layer->surface().scanLine(dy);
                for (int x = 0; x < d; ++x) {
                    const int dx = tl.x() + x;
                    if (dx < 0 || dx >= layer->width())
                        continue;
                    reinterpret_cast<QRgb*>(cur.scanLine(y))[x] = QRgb(row[dx]);
                }
            }

            // A big jump (or a new stroke) starts a fresh smear instead of
            // dragging stale colours along.
            const double jump = std::hypot(c.x() - m_prevCenter.x(), c.y() - m_prevCenter.y());
            if (!m_haveCarry || m_carry.size() != cur.size() || jump > d) {
                m_carry = cur;
                m_haveCarry = true;
                m_prevCenter = c;
                return;
            }

            QImage mixed = cur;
            for (int y = 0; y < d; ++y) {
                QRgb* dst = reinterpret_cast<QRgb*>(mixed.scanLine(y));
                const QRgb* cc = reinterpret_cast<const QRgb*>(m_carry.constScanLine(y));
                for (int x = 0; x < d; ++x)
                    dst[x] = QRgb(lerpPixel(pixel_t(cc[x]), pixel_t(dst[x]), strength));
            }

            for (int y = 0; y < d; ++y) {
                const int dy = tl.y() + y;
                if (dy < 0 || dy >= layer->height())
                    continue;
                pixel_t* row = layer->surface().scanLine(dy);
                const QRgb* mrow = reinterpret_cast<const QRgb*>(mixed.constScanLine(y));
                const uchar* srow = st.constScanLine(y);
                for (int x = 0; x < d; ++x) {
                    int cov = srow[x];
                    if (!cov)
                        continue;
                    const int dx = tl.x() + x;
                    if (dx < 0 || dx >= layer->width())
                        continue;
                    cov = cov * opacity / 100;
                    cov = cov * selectionValue(sel, dx, dy) / 255;
                    if (!cov || !layer->canPaintAt(dx, dy))
                        continue;
                    const pixel_t p = pixel_t(mrow[x]);
                    row[dx] = composePixel(row[dx], qPremult(quint8(cov), getR(p), getG(p), getB(p)),
                                           BlendMode::Normal);
                }
            }
            m_carry = mixed;
            m_prevCenter = c;
            requestUpdate(area);
        });
    }

private:
    ToolOptions::SliderRow m_size;
    ToolOptions::SliderRow m_strength;
    int m_smudgeStrength = 50;
    QImage m_carry;
    QPointF m_prevCenter;
    bool m_haveCarry = false;
};

// ---------------------------------------------------------------- dodge/burn

class DodgeBurnTool : public StrokeToolBase
{
public:
    explicit DodgeBurnTool(QObject* parent) : StrokeToolBase(parent) {}

    QString id() const override { return QStringLiteral("dodgeburn"); }
    QString name() const override { return tr("Dodge / Burn"); }
    QString toolTip() const override { return tr("Lighten (dodge) or darken (burn) the layer"); }
    QString shortcutString() const override { return QStringLiteral("Shift+D"); }
    int sortOrder() const override { return 5; }

    QWidget* createOptionsWidget(QWidget* parent) override
    {
        QBoxLayout* lay = nullptr;
        QWidget* root = ToolOptions::createRoot(parent, &lay);

        m_modeCombo = static_cast<QComboBox*>(
            ToolOptions::addCombo(lay, tr("Mode"), QStringList{ tr("Dodge"), tr("Burn") }, 0));
        connect(m_modeCombo, &QComboBox::currentIndexChanged, this, [this](int idx) {
            m_dodge = (idx == 0);
            emit optionsChanged();
        });

        m_size = ToolOptions::addSlider(lay, tr("Size"), 1, 500, brush().size(), tr(" px"));
        connect(m_size.slider, &QSlider::valueChanged, this, [this](int v) {
            brush().setSize(v);
            emit optionsChanged();
        });

        m_range = ToolOptions::addSlider(lay, tr("Range"), 0, 100, m_rangeValue, QStringLiteral(" %"));
        connect(m_range.slider, &QSlider::valueChanged, this, [this](int v) {
            m_rangeValue = v;
            emit optionsChanged();
        });

        m_optionsWidget = root;
        return root;
    }

    QString statusText() const override
    {
        return m_dodge ? tr("Dodge lightens the pixels under the brush.")
                       : tr("Burn darkens the pixels under the brush.");
    }

protected:
    void paintSegment(const QPointF& a, const QPointF& b, Qt::KeyboardModifiers mods) override
    {
        Q_UNUSED(mods);
        Layer* layer = activeLayer();
        if (!layer)
            return;
        const QRect area = areaFor(a, b, brush().size()).intersected(layer->bounds());
        if (area.isEmpty())
            return;
        touchArea(area);

        const QImage cov = stampCoverage(area, a, b, brush());
        const int opacity = brush().opacity();
        const int range = qBound(0, m_rangeValue, 100);
        const Selection* sel = selectionOrNull();
        const bool dodge = m_dodge;

        for (int y = area.top(); y <= area.bottom(); ++y) {
            pixel_t* row = layer->surface().scanLine(y);
            for (int x = area.left(); x <= area.right(); ++x) {
                const int cov2 =
                    coverageAt(cov, x, y, x - area.left(), y - area.top(), opacity, sel);
                if (!cov2 || !layer->canPaintAt(x, y))
                    continue;
                const pixel_t p = row[x];
                const quint8 alpha = getA(p);
                if (!alpha)
                    continue;
                const int f = qBound(0, cov2 * range / 100, 255);
                if (!f)
                    continue;
                auto shade = [&](quint8 v) {
                    const int nv = dodge ? v + (255 - v) * f / 255 : v - v * f / 255;
                    return quint8(qBound(0, nv, 255));
                };
                row[x] = qPremult(alpha, shade(getR(p)), shade(getG(p)), shade(getB(p)));
            }
        }
        requestUpdate(area);
    }

private:
    ToolOptions::SliderRow m_size;
    ToolOptions::SliderRow m_range;
    QComboBox* m_modeCombo = nullptr;
    int m_rangeValue = 50;
    bool m_dodge = true;
};

// ------------------------------------------------------------------ recolor

/// Replaces the colours it is dragged over, optionally shifting them in HSL.
class RecolorTool : public StrokeToolBase
{
public:
    explicit RecolorTool(QObject* parent) : StrokeToolBase(parent) {}

    QString id() const override { return QStringLiteral("recolor"); }
    QString name() const override { return tr("Recolor"); }
    QString toolTip() const override { return tr("Recolor the pixels you drag over"); }
    QString shortcutString() const override { return QStringLiteral("Shift+R"); }
    int sortOrder() const override { return 6; }

    QWidget* createOptionsWidget(QWidget* parent) override
    {
        QBoxLayout* lay = nullptr;
        QWidget* root = ToolOptions::createRoot(parent, &lay);

        m_modeCombo = static_cast<QComboBox*>(ToolOptions::addCombo(
            lay, tr("Draw mode"), QStringList{ tr("Recolor"), tr("Color replacement") }, 0));
        connect(m_modeCombo, &QComboBox::currentIndexChanged, this, [this](int idx) {
            m_replace = (idx == 1);
            emit optionsChanged();
        });

        m_fuzziness = ToolOptions::addSlider(lay, tr("Fuzziness"), 0, 255, m_fuzz, QStringLiteral(" %"));
        connect(m_fuzziness.slider, &QSlider::valueChanged, this, [this](int v) {
            m_fuzz = v;
            emit optionsChanged();
        });

        m_size = ToolOptions::addSlider(lay, tr("Size"), 1, 500, brush().size(), tr(" px"));
        connect(m_size.slider, &QSlider::valueChanged, this, [this](int v) {
            brush().setSize(v);
            emit optionsChanged();
        });

        m_hue = ToolOptions::addSpin(lay, tr("Hue"), -180, 180, m_hueShift, tr(" deg"));
        connect(static_cast<QSpinBox*>(m_hue), QOverload<int>::of(&QSpinBox::valueChanged), this,
                [this](int v) {
                    m_hueShift = v;
                    emit optionsChanged();
                });

        m_sat = ToolOptions::addSpin(lay, tr("Saturation"), -100, 100, m_satShift, tr(" %"));
        connect(static_cast<QSpinBox*>(m_sat), QOverload<int>::of(&QSpinBox::valueChanged), this,
                [this](int v) {
                    m_satShift = v;
                    emit optionsChanged();
                });

        m_lum = ToolOptions::addSpin(lay, tr("Luminosity"), -100, 100, m_lumShift, tr(" %"));
        connect(static_cast<QSpinBox*>(m_lum), QOverload<int>::of(&QSpinBox::valueChanged), this,
                [this](int v) {
                    m_lumShift = v;
                    emit optionsChanged();
                });

        m_optionsWidget = root;
        return root;
    }

    QString statusText() const override
    {
        return tr("The colour under the cursor is the one that gets replaced (fuzziness %1).")
            .arg(m_fuzz);
    }

protected:
    void beginStroke(const QPointF& p, Qt::KeyboardModifiers mods) override
    {
        Q_UNUSED(mods);
        m_target = sampleColor(p);
        m_haveTarget = true;
    }

    void endStroke() override
    {
        m_haveTarget = false;
    }

    void paintSegment(const QPointF& a, const QPointF& b, Qt::KeyboardModifiers mods) override
    {
        Q_UNUSED(mods);
        Layer* layer = activeLayer();
        if (!layer || !m_haveTarget)
            return;
        const QRect area = areaFor(a, b, brush().size()).intersected(layer->bounds());
        if (area.isEmpty())
            return;
        touchArea(area);

        const QImage cov = stampCoverage(area, a, b, brush());
        const int opacity = brush().opacity();
        const Selection* sel = selectionOrNull();
        const QColor base = toQColor(m_color);

        for (int y = area.top(); y <= area.bottom(); ++y) {
            pixel_t* row = layer->surface().scanLine(y);
            for (int x = area.left(); x <= area.right(); ++x) {
                const pixel_t p = row[x];
                if (!getA(p))
                    continue;
                // "Color replacement" only touches pixels that match the colour
                // sampled at the start of the stroke. "Recolor" instead shifts
                // every pixel the brush passes over, which is what makes the
                // tool useful on a mixed image.
                if (m_replace && colorDistance(p, m_target) > m_fuzz)
                    continue;
                const int c =
                    coverageAt(cov, x, y, x - area.left(), y - area.top(), opacity, sel);
                if (!c || !layer->canPaintAt(x, y))
                    continue;
                const QColor src = toQColor(p);
                QColor dst = m_replace ? base : shiftedColor(src);
                dst.setAlphaF(getA(p) / 255.0);
                const pixel_t np = toPixel(dst);
                if (np == p)
                    continue;
                row[x] = composePixel(row[x], qPremult(quint8(c), getR(np), getG(np), getB(np)),
                                      BlendMode::Normal);
            }
        }
        requestUpdate(area);
    }

private:
    pixel_t sampleColor(const QPointF& p) const
    {
        const int x = int(std::floor(p.x()));
        const int y = int(std::floor(p.y()));
        if (m_doc) {
            const Surface comp = m_doc->compositeSurface();
            if (x >= 0 && y >= 0 && x < comp.width() && y < comp.height())
                return comp.pixel(x, y);
        }
        return 0u;
    }

    QColor shiftedColor(QColor c) const
    {
        if (m_hueShift)
            c = shiftHue(c, m_hueShift);
        if (m_satShift)
            c = shiftSaturation(c, m_satShift);
        if (m_lumShift)
            c = shiftLightness(c, m_lumShift);
        return c;
    }

    ToolOptions::SliderRow m_fuzziness;
    ToolOptions::SliderRow m_size;
    QComboBox* m_modeCombo = nullptr;
    QWidget* m_hue = nullptr;
    QWidget* m_sat = nullptr;
    QWidget* m_lum = nullptr;
    int m_fuzz = 40;
    int m_hueShift = 0;
    int m_satShift = 0;
    int m_lumShift = 0;
    bool m_replace = false;
    bool m_haveTarget = false;
    pixel_t m_target = 0u;
};

} // namespace

QList<Tool*> createDrawingTools()
{
    return QList<Tool*> { new PencilTool(nullptr),     new BrushTextureTool(nullptr),
                          new EraserTool(nullptr),     new BlurTool(nullptr),
                          new SmudgeTool(nullptr),     new DodgeBurnTool(nullptr),
                          new RecolorTool(nullptr) };
}

} // namespace pnq
