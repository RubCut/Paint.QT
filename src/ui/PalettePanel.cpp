#include "ui/PalettePanel.h"

#include <QFontMetrics>
#include <QHBoxLayout>
#include <QLinearGradient>
#include <QMouseEvent>
#include <QPainter>
#include <QToolButton>
#include <QVBoxLayout>

namespace pnq {

PalettePanel::PalettePanel(const QString& title, QWidget* content, QWidget* parent)
    : QWidget(parent), m_title(title), m_content(content)
{
    // The panel paints its whole rectangle, so Qt must be told: without this
    // the region a moving panel vacates is never invalidated, and the previous
    // position stays on screen as a ghost next to the new one.
    setAttribute(Qt::WA_OpaquePaintEvent, true);
    setAttribute(Qt::WA_StyledBackground, false);
    setAutoFillBackground(false);
    setMouseTracking(true);

    // The layout is the single owner of the content geometry. Setting it by
    // hand in resizeEvent() as well gave two owners that fought each other
    // while the panel was dragged, which showed up as flicker.
    m_body = new QVBoxLayout(this);
    m_body->setContentsMargins(1, kTitleHeight + 1, 1, 1);
    m_body->setSpacing(0);
    if (m_content) {
        m_content->setParent(this);
        m_content->show();
        m_body->addWidget(m_content, 1);
    }

    // The close button is painted by hand in the title bar; a real QToolButton
    // would be clipped by the panel's own top edge.
    m_close = nullptr;
}

void PalettePanel::setSizeLimits(const QSize& min, const QSize& max)
{
    setMinimumSize(min);
    setMaximumSize(max);
}

QSize PalettePanel::sizeHint() const
{
    QSize s = m_content ? m_content->sizeHint() : QSize(240, 220);
    s.setWidth(qMax(s.width(), 150));
    s.setHeight(qMax(s.height(), 120) + kTitleHeight + 2);
    return s;
}

QSize PalettePanel::minimumSizeHint() const
{
    QSize s = m_content ? m_content->minimumSizeHint() : QSize(150, 100);
    s.setWidth(qMax(s.width(), 140));
    s.setHeight(qMax(s.height(), 90) + kTitleHeight + 2);
    return s;
}

QRect PalettePanel::gripRect() const
{
    return QRect(width() - kGripSize, height() - kGripSize, kGripSize, kGripSize);
}

bool PalettePanel::inGrip(const QPoint& p) const
{
    return gripRect().adjusted(-3, -3, 3, 3).contains(p);
}

void PalettePanel::moveInside(const QPoint& pos, const QRect& bounds)
{
    if (!parentWidget())
        return;
    // The clamp has to respect the bounds' own origin: the canvas rect does not
    // start at 0,0, and measuring from the origin put every panel a strip inside
    // the edge instead of 6 px from it.
    const int maxX = bounds.left() + bounds.width() - width();
    const int maxY = bounds.top() + bounds.height() - height();
    QPoint p = pos;
    p.setX(qBound(bounds.left(), p.x(), qMax(bounds.left(), maxX)));
    p.setY(qBound(bounds.top(), p.y(), qMax(bounds.top(), maxY)));
    // Compare against where the widget actually is, not against the requested
    // point: the two are equal whenever no clamping was needed, and returning
    // there skipped the move entirely.
    if (p == this->pos())
        return;
    // Whatever the panel covered before must be repainted in the same frame, or
    // the old position stays visible next to the new one while dragging.
    const QRect vacated = geometry();
    move(p);
    if (parentWidget())
        parentWidget()->update(vacated);
    m_savedPos = p;
}

void PalettePanel::pinToCorner(int horizontal, int vertical, const QRect& bounds)
{
    // Sizing is done once. Calling adjustSize() on every re-pin fed the panel's
    // own size change back into the anchoring code and made the panels twitch
    // while the window was being dragged.
    if (!m_sized) {
        adjustSize();
        m_sized = true;
    }
    const int x = horizontal == 0 ? bounds.left() + 6 : bounds.right() - width() - 6 + 1;
    const int y = vertical == 2 ? bounds.top() + 6 : bounds.bottom() - height() - 6 + 1;
    moveInside(QPoint(x, y), bounds);
}

void PalettePanel::clampIntoParent()
{
    if (!parentWidget())
        return;
    const QRect r(parentWidget()->rect());
    moveInside(pos(), r);
}

void PalettePanel::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    // Everything is taken from the widget palette so the panel follows the
    // application theme; hard-coded light greys left a bright strip at the
    // bottom of the panel in dark mode.
    const QPalette pal = palette();
    const QColor window = pal.color(QPalette::Window);
    const QColor text = pal.color(QPalette::WindowText);
    const QColor border = pal.color(QPalette::Mid);
    const QColor edge = pal.color(QPalette::Dark);

    // Panel body, with a thin border like the original's chrome.
    p.setPen(QPen(border, 1));
    p.setBrush(window);
    p.drawRect(rect().adjusted(0, 0, -1, -1));

    // Title bar: the vertical gradient Paint.NET uses on its palette windows.
    QLinearGradient g(0, 0, 0, kTitleHeight);
    g.setColorAt(0.0, pal.color(QPalette::Light).lighter(102));
    g.setColorAt(0.5, pal.color(QPalette::Light));
    g.setColorAt(1.0, pal.color(QPalette::Mid));
    p.setPen(Qt::NoPen);
    p.setBrush(g);
    p.drawRect(titleBarRect().adjusted(1, 1, -1, 0));
    p.setPen(QPen(border, 1));
    p.setBrush(Qt::NoBrush);
    p.drawLine(1, kTitleHeight, width() - 2, kTitleHeight);

    QFont f = font();
    f.setBold(true);
    p.setFont(f);
    p.setPen(text);
    p.drawText(titleBarRect().adjusted(6, 0, -20, 0), Qt::AlignLeft | Qt::AlignVCenter, m_title);

    // Close cross.
    const int cx = width() - 11;
    const int cy = kTitleHeight / 2;
    p.setPen(QPen(pal.color(QPalette::ButtonText), 1.4, Qt::SolidLine, Qt::RoundCap));
    p.drawLine(cx - 3, cy - 3, cx + 3, cy + 3);
    p.drawLine(cx + 3, cy - 3, cx - 3, cy + 3);

    // Resize grip in the bottom right corner.
    p.setPen(QPen(edge, 1));
    for (int i = 0; i < 3; ++i) {
        p.drawLine(width() - 4 - i * 4, height() - 4, width() - 4, height() - 4 - i * 4);
    }
}

void PalettePanel::mousePressEvent(QMouseEvent* e)
{
    if (e->button() != Qt::LeftButton) {
        QWidget::mousePressEvent(e);
        return;
    }
    // The close box sits on top of the title bar.
    if (inTitleBar(e->pos()) && e->pos().x() >= width() - 20) {
        emit closeRequested(this);
        return;
    }
    if (inGrip(e->pos())) {
        m_resizing = true;
        m_resizeFrom = e->globalPosition().toPoint();
        m_resizeStart = size();
        return;
    }
    if (inTitleBar(e->pos())) {
        m_dragging = true;
        m_pinned = false;
        m_dragOffset = e->position().toPoint();
        raise();
        return;
    }
    QWidget::mousePressEvent(e);
}

void PalettePanel::mouseMoveEvent(QMouseEvent* e)
{
    if (m_resizing) {
        const QPoint d = e->globalPosition().toPoint() - m_resizeFrom;
        const QSize min = minimumSizeHint();
        const QSize want(m_resizeStart.width() + d.x(), m_resizeStart.height() + d.y());
        resize(qMax(min.width(), want.width()), qMax(min.height(), want.height()));
        clampIntoParent();
        return;
    }
    if (m_dragging && parentWidget()) {
        // e->position() is relative to this panel, but moveInside() works in the
        // parent's coordinates. Using the panel-local point directly made the
        // panel jump to the parent's origin on the first move and then crawl
        // diagonally, which is the "returns to the top left corner" flicker.
        const QPoint inParent = parentWidget()->mapFromGlobal(e->globalPosition().toPoint());
        moveInside(inParent - m_dragOffset, parentWidget()->rect());
        return;
    }
    if (inGrip(e->pos()) || inTitleBar(e->pos()))
        setCursor(e->pos().x() >= width() - 20 && inTitleBar(e->pos()) ? Qt::ArrowCursor
                                                                        : Qt::SizeAllCursor);
    else
        unsetCursor();
}

void PalettePanel::mouseReleaseEvent(QMouseEvent* e)
{
    Q_UNUSED(e);
    if (m_resizing) {
        m_resizing = false;
        emit resizedByUser();
        return;
    }
    if (m_dragging) {
        m_dragging = false;
        emit movedByUser();
    }
}

void PalettePanel::mouseDoubleClickEvent(QMouseEvent* e)
{
    // Double clicking the title bar re-pins the panel to its default corner.
    if (inTitleBar(e->pos()) && parentWidget()) {
        const QRect r(parentWidget()->rect());
        const int horizontal = pos().x() + width() / 2 < r.center().x() ? 0 : 1;
        const int vertical = pos().y() + height() / 2 < r.center().y() ? 2 : 3;
        pinToCorner(horizontal, vertical, r);
        return;
    }
    QWidget::mouseDoubleClickEvent(e);
}

void PalettePanel::resizeEvent(QResizeEvent* e)
{
    QWidget::resizeEvent(e);
    // The layout places the content; nothing to do here beyond letting Qt know
    // the widget is opaque over its whole rect.
}

} // namespace pnq
