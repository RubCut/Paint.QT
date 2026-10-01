#pragma once

#include <QPoint>
#include <QRect>
#include <QWidget>

class QHBoxLayout;
class QToolButton;
class QVBoxLayout;

namespace pnq {

/// A palette presented as a small floating panel inside the main window.
///
/// Paint.NET shows Layers, Colors and History as little windows that float
/// over the canvas. Qt's floating QDockWidget cannot do that on Wayland, where
/// a client is not allowed to position its own top level windows, so the panel
/// is an ordinary child widget that is painted above the canvas, dragged by its
/// title bar and kept inside the window.
class PalettePanel : public QWidget
{
    Q_OBJECT
public:
    PalettePanel(const QString& title, QWidget* content, QWidget* parent);

    QWidget* content() const { return m_content; }
    QString title() const { return m_title; }

    /// Places the panel so that its top-left corner sits at `pos`, clamped so
    /// the panel stays inside `bounds`.
    void moveInside(const QPoint& pos, const QRect& bounds);
    /// Pins the panel to a corner of `bounds`: 0 = left, 1 = right,
    /// 2 = top, 3 = bottom (matching the caller's choice of edge).
    void pinToCorner(int horizontal, int vertical, const QRect& bounds);

    /// Caps the panel so a palette with a large grid (Colors) cannot grow into
    /// a column that swallows the canvas.
    void setSizeLimits(const QSize& min, const QSize& max);

    /// Remembered between runs.
    QPoint savedPos() const { return m_savedPos; }
    void setSavedPos(const QPoint& p) { m_savedPos = p; }
    bool isPinned() const { return m_pinned; }
    void setPinned(bool on) { m_pinned = on; }

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

signals:
    void closeRequested(PalettePanel* self);
    void movedByUser();
    void resizedByUser();

protected:
    void paintEvent(QPaintEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void mouseDoubleClickEvent(QMouseEvent* e) override;
    void resizeEvent(QResizeEvent* e) override;

private:
    static constexpr int kTitleHeight = 20;
    static constexpr int kGripSize = 14;

    bool inTitleBar(const QPoint& p) const { return p.y() < kTitleHeight; }
    bool inGrip(const QPoint& p) const;
    QRect titleBarRect() const { return QRect(0, 0, width(), kTitleHeight); }
    QRect gripRect() const;
    void clampIntoParent();

    QString m_title;
    QWidget* m_content = nullptr;
    QVBoxLayout* m_body = nullptr;
    QToolButton* m_close = nullptr;
    QPoint m_dragOffset;
    QPoint m_resizeFrom;
    QSize m_resizeStart;
    QPoint m_savedPos;
    bool m_pinned = true;
    /// The panel has picked its default size; do not resize it again on re-pin.
    bool m_sized = false;
    bool m_dragging = false;
    bool m_resizing = false;
};

} // namespace pnq
