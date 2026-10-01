// Fill / navigation tools: paint bucket, gradient, colour picker, zoom and pan.
//
// The bucket and the gradient mutate the active layer through a StrokeBuffer
// so that a single undo step restores everything they touched. The gradient
// renders into a scratch surface which is then composited with the selected
// blend mode, which keeps the "paint while dragging, commit on release"
// protocol of the drawing tools.
#include "tools/Tool.h"

#include "core/BlendMode.h"
#include "core/Brush.h"
#include "core/ColorUtils.h"
#include "core/Document.h"
#include "core/Gradient.h"
#include "core/History.h"
#include "core/ImageMath.h"
#include "core/Layer.h"
#include "core/Selection.h"
#include "core/Surface.h"
#include "tools/ToolSupport.h"
#include "ui/CanvasView.h"
#include "ui/ToolOptions.h"

#include <QAbstractButton>
#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QBoxLayout>
#include <QWidget>
#include <cmath>
#include <functional>

namespace pnq {

namespace {

inline int selectionValue(const Selection* sel, int x, int y)
{
    return sel ? sel->at(x, y) : 255;
}

/// Adds a check box to the layout and wires it to a member flag.
void wireCheck(QCheckBox* cb, bool* flag, QObject* context)
{
    QObject::connect(cb, &QCheckBox::toggled, context, [flag](bool on) { *flag = on; });
}

/// Creates a push button in the layout connected to `slot`.
QPushButton* addAction(QBoxLayout* lay, const QString& text, QObject* context, std::function<void()> slot)
{
    QPushButton* b = static_cast<QPushButton*>(ToolOptions::addPushButton(lay, text));
    QObject::connect(b, &QAbstractButton::clicked, context, [slot]() { slot(); });
    return b;
}

// ============================================================ paint bucket

class BucketTool : public Tool
{
public:
    explicit BucketTool(QObject* parent) : Tool(parent) {}

    QString id() const override { return QStringLiteral("bucket"); }
    QString name() const override { return tr("Paint Bucket"); }
    QString toolTip() const override { return tr("Flood fill an area with the foreground colour"); }
    QString shortcutString() const override { return QStringLiteral("P"); }
    int sortOrder() const override { return 20; }
    Qt::CursorShape cursorShape() const override { return Qt::CrossCursor; }

    QWidget* createOptionsWidget(QWidget* parent) override
    {
        QBoxLayout* lay = nullptr;
        QWidget* root = ToolOptions::createRoot(parent, &lay);

        m_tolerance = ToolOptions::addSlider(lay, tr("Tolerance"), 0, 255, m_tol, QStringLiteral(" %"));
        connect(m_tolerance.slider, &QSlider::valueChanged, this, [this](int v) {
            m_tol = v;
            emit optionsChanged();
        });

        m_fillCombo = static_cast<QComboBox*>(
            ToolOptions::addCombo(lay, tr("Fill"), QStringList{ tr("Foreground"), tr("Background") }, 0));
        connect(m_fillCombo, &QComboBox::currentIndexChanged, this, [this](int idx) {
            m_useBackground = (idx == 1);
            emit optionsChanged();
        });

        m_opacity = ToolOptions::addSlider(lay, tr("Opacity"), 1, 100, m_alpha, QStringLiteral(" %"));
        connect(m_opacity.slider, &QSlider::valueChanged, this, [this](int v) {
            m_alpha = v;
            emit optionsChanged();
        });

        wireCheck(static_cast<QCheckBox*>(ToolOptions::addCheck(lay, tr("Sample all layers"), m_sampleAllLayers)),
                  &m_sampleAllLayers, this);
        wireCheck(static_cast<QCheckBox*>(ToolOptions::addCheck(lay, tr("Contiguous"), m_contiguous)),
                  &m_contiguous, this);

        m_optionsWidget = root;
        return root;
    }

    void mouseDown(const QPoint& docPos, Qt::MouseButton button, Qt::KeyboardModifiers mods) override
    {
        Q_UNUSED(mods);
        finishFill();
        if (!canPaint() || !activeLayer() || (button != Qt::LeftButton && button != Qt::RightButton))
            return;
        m_layerIndex = activeLayerIndex();
        m_buffer.begin(&activeLayer()->surface());
        m_filling = true;
        m_button = button;
        m_color = fillColorFor(button);
        fillAt(docPos);
    }

    void mouseMove(const QPoint& docPos, Qt::MouseButtons buttons, Qt::KeyboardModifiers mods) override
    {
        Q_UNUSED(mods);
        if (m_filling && (buttons & m_button))
            fillAt(docPos);
    }

    void mouseUp(const QPoint&, Qt::MouseButton, Qt::KeyboardModifiers) override { finishFill(); }

    void deactivate() override
    {
        finishFill();
        Tool::deactivate();
    }

    QString statusText() const override
    {
        return tr("Tolerance %1 - the fill colour comes from the %2 button.")
            .arg(m_tol)
            .arg(m_button == Qt::RightButton ? tr("right") : tr("left"));
    }

private:
    pixel_t fillColorFor(Qt::MouseButton button) const
    {
        if (m_useBackground)
            return m_secondary;
        return button == Qt::RightButton ? m_secondary : m_primary;
    }

    /// Mask of every pixel that has to be filled.
    QImage buildMask(const Surface& src, const QPoint& seed, int tolerance, bool contiguous) const
    {
        if (contiguous) {
            // fillSelection == 0 makes floodFill only produce the mask.
            Surface work = src.copy();
            QImage mask;
            ImageMath::floodFill(work, seed, 0u, tolerance, 0, &mask);
            return mask;
        }
        // Global (non contiguous) fill: everything within tolerance anywhere.
        QImage mask(src.size(), QImage::Format_Alpha8);
        mask.fill(0);
        if (seed.x() < 0 || seed.y() < 0 || seed.x() >= src.width() || seed.y() >= src.height())
            return QImage();
        const pixel_t target = src.pixel(seed.x(), seed.y());
        for (int y = 0; y < src.height(); ++y) {
            uchar* row = mask.scanLine(y);
            const pixel_t* srow = src.scanLine(y);
            for (int x = 0; x < src.width(); ++x) {
                if (colorDistance(srow[x], target) <= tolerance)
                    row[x] = 255;
            }
        }
        return mask;
    }

    void fillAt(const QPoint& seed)
    {
        Layer* layer = activeLayer();
        if (!layer || !m_doc)
            return;
        if (seed.x() < 0 || seed.y() < 0 || seed.x() >= layer->width() || seed.y() >= layer->height())
            return;

        const Surface src = m_sampleAllLayers ? m_doc->compositeSurface() : layer->surface();
        const QImage mask = buildMask(src, seed, m_tol, m_contiguous);
        if (mask.isNull())
            return;
        const QRect bounds = QRect(0, 0, mask.width(), mask.height());
        if (!bounds.intersects(layer->bounds()))
            return;

        m_buffer.touch(bounds);

        const Selection* sel = selectionOrNull();
        Surface& target = layer->surface();
        for (int y = 0; y < mask.height() && y < target.height(); ++y) {
            const uchar* mrow = mask.constScanLine(y);
            pixel_t* row = target.scanLine(y);
            for (int x = 0; x < mask.width() && x < target.width(); ++x) {
                int a = mrow[x];
                if (!a)
                    continue;
                a = a * m_alpha / 100;
                a = a * selectionValue(sel, x, y) / 255;
                if (!a || !layer->canPaintAt(x, y))
                    continue;
                row[x] = composePixel(row[x], qPremult(quint8(a), getR(m_color), getG(m_color),
                                                      getB(m_color)),
                                      BlendMode::Normal);
            }
        }
        requestUpdate(layer->bounds());
    }

    void finishFill()
    {
        if (!m_filling)
            return;
        m_filling = false;
        const QRect dirty = m_buffer.end();
        if (dirty.isEmpty())
            return;
        pushStrokeAction(this, name(), m_layerIndex, m_buffer);
    }

    ToolOptions::SliderRow m_tolerance;
    ToolOptions::SliderRow m_opacity;
    QComboBox* m_fillCombo = nullptr;
    StrokeBuffer m_buffer;
    bool m_filling = false;
    bool m_sampleAllLayers = false;
    bool m_contiguous = true;
    bool m_useBackground = false;
    int m_tol = 32;
    int m_alpha = 100;
    int m_layerIndex = -1;
    Qt::MouseButton m_button = Qt::LeftButton;
    pixel_t m_color = 0xFF000000u;
};

// ================================================================ gradient

class GradientTool : public Tool
{
public:
    explicit GradientTool(QObject* parent) : Tool(parent) {}

    QString id() const override { return QStringLiteral("gradient"); }
    QString name() const override { return tr("Gradient"); }
    QString toolTip() const override { return tr("Drag to paint a gradient between the two colours"); }
    QString shortcutString() const override { return QStringLiteral("F"); }
    int sortOrder() const override { return 21; }
    Qt::CursorShape cursorShape() const override { return Qt::CrossCursor; }

    QWidget* createOptionsWidget(QWidget* parent) override
    {
        QBoxLayout* lay = nullptr;
        QWidget* root = ToolOptions::createRoot(parent, &lay);

        QStringList modes;
        for (int i = 0; i <= int(GradientMode::Rhombus); ++i)
            modes << gradientModeName(GradientMode(i));
        m_modeCombo = static_cast<QComboBox*>(ToolOptions::addCombo(lay, tr("Mode"), modes, 0));
        connect(m_modeCombo, &QComboBox::currentIndexChanged, this, [this](int idx) {
            m_mode = GradientMode(qBound(0, idx, int(GradientMode::Rhombus)));
            emit optionsChanged();
        });

        m_loopCombo = static_cast<QComboBox*>(ToolOptions::addCombo(
            lay, tr("Loop mode"), QStringList{ tr("Once"), tr("Loop"), tr("Mirror") }, 0));
        connect(m_loopCombo, &QComboBox::currentIndexChanged, this, [this](int idx) {
            m_loop = LoopMode(qBound(0, idx, 2));
            emit optionsChanged();
        });

        wireCheck(static_cast<QCheckBox*>(ToolOptions::addCheck(lay, tr("Reverse"), m_reverse)),
                  &m_reverse, this);
        wireCheck(static_cast<QCheckBox*>(ToolOptions::addCheck(lay, tr("Dither"), m_dither)),
                  &m_dither, this);

        m_opacity = ToolOptions::addSlider(lay, tr("Opacity"), 1, 100, m_alpha, QStringLiteral(" %"));
        connect(m_opacity.slider, &QSlider::valueChanged, this, [this](int v) {
            m_alpha = v;
            emit optionsChanged();
        });

        m_optionsWidget = root;
        return root;
    }

    void mouseDown(const QPoint& docPos, Qt::MouseButton button, Qt::KeyboardModifiers mods) override
    {
        Q_UNUSED(mods);
        finishGradient();
        if (!canPaint() || !activeLayer())
            return;
        if (button != Qt::LeftButton && button != Qt::RightButton)
            return;
        m_layerIndex = activeLayerIndex();
        m_buffer.begin(&activeLayer()->surface());
        m_dragging = true;
        m_button = button;
        m_start = docPos;
        m_end = docPos;
        paintGradient();
    }

    void mouseMove(const QPoint& docPos, Qt::MouseButtons buttons, Qt::KeyboardModifiers mods) override
    {
        Q_UNUSED(mods);
        m_end = docPos;
        if (m_dragging && (buttons & m_button))
            paintGradient();
        requestOverlayUpdate();
    }

    void mouseUp(const QPoint& docPos, Qt::MouseButton button, Qt::KeyboardModifiers mods) override
    {
        Q_UNUSED(mods);
        if (button == m_button && m_dragging)
            m_end = docPos;
        finishGradient();
        requestOverlayUpdate();
    }

    void deactivate() override
    {
        finishGradient();
        Tool::deactivate();
    }

    void drawOverlay(QPainter& p) override
    {
        if (!m_dragging)
            return;
        const QRect r = QRect(m_start, m_end).normalized();
        const double z = m_view ? m_view->zoom() : 1.0;
        QPen pen(QColor(0, 0, 0, 200));
        pen.setWidthF(1.0 / qMax(0.01, z));
        pen.setStyle(Qt::DashLine);
        p.setPen(pen);
        p.setBrush(Qt::NoBrush);
        p.drawRect(r);
        p.drawLine(m_start, m_end);
    }

    QString statusText() const override
    {
        if (!m_dragging)
            return tr("Drag from one point to the other to define the gradient.");
        const QRect r = QRect(m_start, m_end).normalized();
        return tr("Gradient: %1 x %2 pixels.").arg(r.width()).arg(r.height());
    }

private:
    enum LoopMode { Once = 0, Loop, Mirror };

    Gradient currentGradient() const
    {
        const Gradient* g = m_view ? m_view->activeGradient() : nullptr;
        Gradient out = (g && !g->stops().isEmpty()) ? *g : Gradient::fromTwoColors(m_primary, m_secondary);
        out.setMode(m_mode);
        out.setFocalPoint(QPointF(0.5, 0.5));
        return out;
    }

    /// Maps a raw 0..1 parameter through reverse / loop / mirror.
    double shapeParameter(double t) const
    {
        if (m_reverse)
            t = 1.0 - t;
        switch (m_loop) {
        case Once:
            break;
        case Loop:
            t = t - std::floor(t);
            break;
        case Mirror: {
            double m2 = std::fabs(t);
            m2 = m2 - std::floor(m2 / 2.0) * 2.0;
            t = (m2 > 1.0) ? 2.0 - m2 : m2;
            break;
        }
        }
        return t;
    }

    void paintGradient()
    {
        Layer* layer = activeLayer();
        if (!layer)
            return;
        const QRect r = QRect(m_start, m_end).normalized().intersected(layer->bounds());
        if (r.isEmpty())
            return;
        m_buffer.touch(r);

        const Gradient grad = currentGradient();
        const double w = qMax(1.0, double(r.width()) - 1.0);
        const double h = qMax(1.0, double(r.height()) - 1.0);
        const Selection* sel = selectionOrNull();
        // With no reverse / loop / dither the mapping is exactly colorAt2D().
        const bool plain = !m_reverse && m_loop == Once && !m_dither;

        Surface scratch(r.size());
        scratch.fill(transparentPixel());
        for (int y = r.top(); y <= r.bottom(); ++y) {
            const double v = (y - r.top()) / h;
            pixel_t* row = scratch.scanLine(y - r.top());
            for (int x = r.left(); x <= r.right(); ++x) {
                const double u = (x - r.left()) / w;
                pixel_t c = 0u;
                if (plain) {
                    c = grad.colorAt2D(u, v);
                } else {
                    double t = 0.0;
                    switch (grad.mode()) {
                    case GradientMode::Linear:
                        t = u;
                        break;
                    case GradientMode::Circular:
                        t = 0.5 + std::atan2(v - 0.5, u - 0.5) / (2.0 * M_PI);
                        break;
                    case GradientMode::Reflected:
                        t = u < 0.5 ? 2.0 * u : 2.0 * (1.0 - u);
                        break;
                    case GradientMode::Rhombus:
                        t = (u + v) / 2.0;
                        break;
                    }
                    if (m_dither)
                        t += (ditherOffset(x, y)) / 255.0;
                    c = grad.colorAt(shapeParameter(t));
                }
                row[x - r.left()] = c;
            }
        }

        for (int y = r.top(); y <= r.bottom(); ++y) {
            pixel_t* row = layer->surface().scanLine(y);
            const pixel_t* srow = scratch.scanLine(y - r.top());
            for (int x = r.left(); x <= r.right(); ++x) {
                const pixel_t c = srow[x - r.left()];
                int a = getA(c) * m_alpha / 100;
                a = a * selectionValue(sel, x, y) / 255;
                if (!a || !layer->canPaintAt(x, y))
                    continue;
                row[x] = composePixel(row[x], qPremult(quint8(a), getR(c), getG(c), getB(c)),
                                      BlendMode::Normal);
            }
        }
        requestUpdate(r);
        requestOverlayUpdate();
    }

    /// Stable per pixel dither offset in the -32..32 range (no flicker).
    static int ditherOffset(int x, int y)
    {
        uint h = uint(x) * 73856093u ^ uint(y) * 19349663u;
        h ^= h >> 13;
        h *= 0x5bd1e995u;
        h ^= h >> 15;
        return int(h % 65) - 32;
    }

    void finishGradient()
    {
        m_dragging = false;
        const QRect dirty = m_buffer.end();
        if (dirty.isEmpty())
            return;
        pushStrokeAction(this, name(), m_layerIndex, m_buffer);
    }

    ToolOptions::SliderRow m_opacity;
    QComboBox* m_modeCombo = nullptr;
    QComboBox* m_loopCombo = nullptr;
    StrokeBuffer m_buffer;
    bool m_dragging = false;
    bool m_reverse = false;
    bool m_dither = false;
    LoopMode m_loop = Once;
    GradientMode m_mode = GradientMode::Linear;
    int m_alpha = 100;
    int m_layerIndex = -1;
    QPoint m_start;
    QPoint m_end;
    Qt::MouseButton m_button = Qt::LeftButton;
};

// ================================================================= picker

class PickerTool : public Tool
{
public:
    explicit PickerTool(QObject* parent) : Tool(parent) {}

    QString id() const override { return QStringLiteral("picker"); }
    QString name() const override { return tr("Color Picker"); }
    QString toolTip() const override { return tr("Pick a colour from the image"); }
    QString shortcutString() const override { return QStringLiteral("K"); }
    int sortOrder() const override { return 22; }
    Qt::CursorShape cursorShape() const override { return Qt::UpArrowCursor; }

    QWidget* createOptionsWidget(QWidget* parent) override
    {
        QBoxLayout* lay = nullptr;
        QWidget* root = ToolOptions::createRoot(parent, &lay);

        wireCheck(static_cast<QCheckBox*>(ToolOptions::addCheck(lay, tr("Sample all layers"), m_sampleAllLayers)),
                  &m_sampleAllLayers, this);

        m_modeCombo = static_cast<QComboBox*>(ToolOptions::addCombo(
            lay, tr("Coordinates"), QStringList{ tr("Absolute"), tr("Relative") }, 0));
        connect(m_modeCombo, &QComboBox::currentIndexChanged, this, [this](int idx) {
            m_relative = (idx == 1);
            emit optionsChanged();
        });

        m_optionsWidget = root;
        return root;
    }

    void mouseDown(const QPoint& docPos, Qt::MouseButton button, Qt::KeyboardModifiers mods) override
    {
        Q_UNUSED(mods);
        if (!m_doc || (button != Qt::LeftButton && button != Qt::RightButton))
            return;
        m_button = button;
        m_dragging = true;
        m_origin = docPos;
        m_base = sample(docPos);
        applyPicked(m_base);
    }

    void mouseMove(const QPoint& docPos, Qt::MouseButtons buttons, Qt::KeyboardModifiers mods) override
    {
        Q_UNUSED(mods);
        m_pos = docPos;
        if (!m_dragging || !(buttons & m_button))
            return;
        pixel_t c = m_base;
        if (m_relative) {
            // Dragging changes the hue (x) and the saturation (y) of the colour
            // picked at the press point.
            QColor col = toQColor(m_base);
            const int dh = (docPos.x() - m_origin.x()) * 2;
            const int ds = (docPos.y() - m_origin.y()) / 2;
            if (dh)
                col = shiftHue(col, dh);
            if (ds)
                col = shiftSaturation(col, ds);
            c = toPixel(col);
        } else {
            c = sample(docPos);
        }
        applyPicked(c);
    }

    void mouseUp(const QPoint&, Qt::MouseButton, Qt::KeyboardModifiers) override { m_dragging = false; }

    void mouseDoubleClick(const QPoint&, Qt::MouseButton, Qt::KeyboardModifiers) override {}

    QString statusText() const override
    {
        const QString hex = colorToHex(m_picked, true);
        const QString name = colorName(m_picked);
        return name.isEmpty() ? hex : tr("%1 (%2)").arg(hex, name);
    }

    void drawOverlay(QPainter& p) override
    {
        // A small swatch following the cursor.
        const double z = m_view ? m_view->zoom() : 1.0;
        const QRectF box(m_pos.x() + 12.0, m_pos.y() + 12.0, 18.0 / qMax(0.01, z) * qMax(0.01, z),
                         18.0);
        p.fillRect(box, toQColor(m_picked));
        p.setPen(QPen(QColor(0, 0, 0), 1.0 / qMax(0.01, z)));
        p.setBrush(Qt::NoBrush);
        p.drawRect(box);
    }

private:
    pixel_t sample(const QPoint& p) const
    {
        if (!m_doc)
            return 0u;
        if (p.x() < 0 || p.y() < 0 || p.x() >= m_doc->width() || p.y() >= m_doc->height())
            return 0u;
        if (m_sampleAllLayers)
            return m_doc->compositeSurface().pixel(p.x(), p.y());
        Layer* l = m_doc->activeLayer();
        return l ? l->surface().pixel(p.x(), p.y()) : 0u;
    }

    void applyPicked(pixel_t c)
    {
        m_picked = c;
        if (m_button == Qt::RightButton)
            setSecondaryColor(c);
        else
            setPrimaryColor(c);
        emit colorsUsed(m_primary, m_secondary);
        if (m_view)
            emit cursorMoved(m_pos);
        refreshStatus();
        requestOverlayUpdate();
    }

    QComboBox* m_modeCombo = nullptr;
    bool m_dragging = false;
    bool m_sampleAllLayers = true;
    bool m_relative = false;
    Qt::MouseButton m_button = Qt::LeftButton;
    QPoint m_origin;
    QPoint m_pos;
    pixel_t m_base = 0u;
    pixel_t m_picked = 0xFF000000u;
};

// =================================================================== zoom

class ZoomTool : public Tool
{
public:
    explicit ZoomTool(QObject* parent) : Tool(parent) {}

    QString id() const override { return QStringLiteral("zoom"); }
    QString name() const override { return tr("Zoom"); }
    QString toolTip() const override { return tr("Zoom in, out or into a dragged rectangle"); }
    QString shortcutString() const override { return QStringLiteral("Z"); }
    int sortOrder() const override { return 30; }
    Qt::CursorShape cursorShape() const override { return Qt::CrossCursor; }

    QWidget* createOptionsWidget(QWidget* parent) override
    {
        QBoxLayout* lay = nullptr;
        QWidget* root = ToolOptions::createRoot(parent, &lay);

        addAction(lay, tr("Zoom In"), this, [this] { zoomStep(1.25); });
        addAction(lay, tr("Zoom Out"), this, [this] { zoomStep(1.0 / 1.25); });
        addAction(lay, tr("Zoom to Fit"), this, [this] {
            if (m_view) {
                m_view->zoomToFit();
                refreshStatus();
            }
        });
        addAction(lay, tr("Zoom to Selection"), this, [this] {
            if (m_view) {
                m_view->zoomToSelection();
                refreshStatus();
            }
        });
        addAction(lay, tr("Zoom 100%"), this, [this] {
            if (m_view) {
                m_view->setZoomPercent(100);
                refreshStatus();
            }
        });

        m_optionsWidget = root;
        return root;
    }

    void mouseDown(const QPoint& docPos, Qt::MouseButton button, Qt::KeyboardModifiers mods) override
    {
        Q_UNUSED(mods);
        if (button != Qt::LeftButton && button != Qt::RightButton)
            return;
        m_dragStart = docPos;
        m_dragEnd = docPos;
        m_button = button;
        m_dragging = true;
        m_moved = false;
    }

    void mouseMove(const QPoint& docPos, Qt::MouseButtons buttons, Qt::KeyboardModifiers mods) override
    {
        Q_UNUSED(mods);
        Q_UNUSED(buttons);
        m_dragEnd = docPos;
        const int dx = docPos.x() - m_dragStart.x();
        const int dy = docPos.y() - m_dragStart.y();
        m_moved = m_moved || (qAbs(dx) > 2 && qAbs(dy) > 2);
        requestOverlayUpdate();
    }

    void mouseUp(const QPoint& docPos, Qt::MouseButton button, Qt::KeyboardModifiers mods) override
    {
        Q_UNUSED(button);
        m_dragging = false;
        m_dragEnd = docPos;
        if (!m_view)
            return;
        if (m_moved) {
            const QRect r = QRect(m_dragStart, docPos).normalized();
            zoomToRect(r);
        } else if (mods & Qt::ShiftModifier) {
            zoomStep(1.0 / 1.25, docPos);
        } else {
            zoomStep(1.25, docPos);
        }
        refreshStatus();
        requestOverlayUpdate();
    }

    void wheelEvent(QPoint delta, Qt::KeyboardModifiers mods) override
    {
        Q_UNUSED(mods);
        if (!m_view)
            return;
        const double f = delta.y() >= 0 ? 1.25 : 1.0 / 1.25;
        zoomStep(f, m_view->lastCursorImagePos());
    }

    void drawOverlay(QPainter& p) override
    {
        if (!m_dragging)
            return;
        const QRect r = QRect(m_dragStart, m_dragEnd).normalized();
        const double z = m_view ? m_view->zoom() : 1.0;
        QPen pen(QColor(0, 0, 0, 220));
        pen.setWidthF(1.0 / qMax(0.01, z));
        pen.setStyle(Qt::DashLine);
        p.setPen(pen);
        p.setBrush(Qt::NoBrush);
        p.drawRect(r);
    }

    QString statusText() const override
    {
        if (!m_view)
            return QString();
        const int pct = int(m_view->zoom() * 100.0 + 0.5);
        return tr("Zoom: %1%").arg(pct);
    }

private:
    void zoomStep(double factor, const QPoint& anchor = QPoint())
    {
        if (!m_view)
            return;
        QPoint a = anchor;
        if (a.isNull())
            a = m_view->lastCursorImagePos();
        m_view->setZoom(m_view->zoom() * factor, m_view->imageToWidget(QPointF(a)));
        refreshStatus();
    }

    void zoomToRect(const QRect& r)
    {
        if (!m_view || r.isEmpty() || r.width() < 1 || r.height() < 1)
            return;
        const QRectF view = m_view->viewRect();
        if (view.width() < 1.0 || view.height() < 1.0)
            return;
        const double zx = view.width() / double(r.width());
        const double zy = view.height() / double(r.height());
        const double z = qMin(zx, zy);
        const QPointF anchor = m_view->imageToWidget(QPointF(r.center()));
        m_view->setZoom(z, anchor);
    }

    bool m_dragging = false;
    bool m_moved = false;
    QPoint m_dragStart;
    QPoint m_dragEnd;
    Qt::MouseButton m_button = Qt::LeftButton;
};

// ==================================================================== pan

class PanTool : public Tool
{
public:
    explicit PanTool(QObject* parent) : Tool(parent) {}

    QString id() const override { return QStringLiteral("pan"); }
    QString name() const override { return tr("Pan"); }
    QString toolTip() const override { return tr("Drag to scroll the canvas"); }
    QString shortcutString() const override { return QStringLiteral("H"); }
    int sortOrder() const override { return 31; }
    Qt::CursorShape cursorShape() const override { return Qt::SizeAllCursor; }

    QWidget* createOptionsWidget(QWidget* parent) override
    {
        QBoxLayout* lay = nullptr;
        QWidget* root = ToolOptions::createRoot(parent, &lay);
        addAction(lay, tr("Center Image"), this, [this] {
            if (!m_view || !m_doc)
                return;
            const QPointF c(m_doc->width() / 2.0, m_doc->height() / 2.0);
            const QRectF view = m_view->viewRect();
            m_view->scrollTo(view.center(), c);
            emit updateRequested(m_doc->bounds());
            refreshStatus();
        });
        m_optionsWidget = root;
        return root;
    }

    void mouseDown(const QPoint& docPos, Qt::MouseButton button, Qt::KeyboardModifiers mods) override
    {
        Q_UNUSED(mods);
        if (!m_view)
            return;
        if (button != Qt::LeftButton && button != Qt::RightButton && button != Qt::MiddleButton)
            return;
        m_panning = true;
        m_button = button;
        m_anchorImage = QPointF(docPos);
        m_anchorWidget = m_view->imageToWidget(m_anchorImage);
    }

    void mouseMove(const QPoint& docPos, Qt::MouseButtons buttons, Qt::KeyboardModifiers mods) override
    {
        Q_UNUSED(mods);
        if (!m_panning || !m_view || !(buttons & m_button))
            return;
        // Keep the image point grabbed at press time under the cursor.
        const double z = m_view->zoom();
        const QPointF widget(m_anchorWidget.x() + (docPos.x() - m_anchorImage.x()) * z,
                             m_anchorWidget.y() + (docPos.y() - m_anchorImage.y()) * z);
        m_view->scrollTo(widget, m_anchorImage);
        emit updateRequested(m_doc ? m_doc->bounds() : QRect());
        refreshStatus();
    }

    void mouseUp(const QPoint&, Qt::MouseButton, Qt::KeyboardModifiers) override { m_panning = false; }

    void wheelEvent(QPoint delta, Qt::KeyboardModifiers mods) override
    {
        Q_UNUSED(mods);
        if (!m_view)
            return;
        const double f = delta.y() >= 0 ? 1.25 : 1.0 / 1.25;
        const QPoint anchor = m_view->lastCursorImagePos();
        m_view->setZoom(m_view->zoom() * f, m_view->imageToWidget(QPointF(anchor)));
        emit updateRequested(m_doc ? m_doc->bounds() : QRect());
        refreshStatus();
    }

    QString statusText() const override
    {
        if (!m_view)
            return QString();
        return tr("Zoom: %1%").arg(int(m_view->zoom() * 100.0 + 0.5));
    }

private:
    bool m_panning = false;
    Qt::MouseButton m_button = Qt::LeftButton;
    QPointF m_anchorImage;
    QPointF m_anchorWidget;
};

} // namespace

QList<Tool*> createFillTools()
{
    return QList<Tool*> { new BucketTool(nullptr), new GradientTool(nullptr), new PickerTool(nullptr),
                          new ZoomTool(nullptr),    new PanTool(nullptr) };
}

} // namespace pnq
