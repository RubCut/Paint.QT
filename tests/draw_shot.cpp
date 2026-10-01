// Visual proof: draws with several tools and saves the canvas so the result can
// be looked at, not just asserted about.
#include "core/Document.h"
#include "core/Selection.h"
#include "tools/ToolManager.h"
#include "ui/CanvasView.h"
#include "ui/MainWindow.h"

#include <QAction>
#include <QApplication>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QPixmap>
#include <cstdio>

using namespace pnq;

namespace {
MainWindow* g_win;
CanvasView* g_canvas;

void pump(int n = 8)
{
    for (int i = 0; i < n; ++i)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
}
QAction* act(const QString& id)
{
    for (QAction* a : g_win->findChildren<QAction*>())
        if (a->objectName() == id)
            return a;
    return nullptr;
}
void useTool(const QString& id)
{
    act(QStringLiteral("tool.") + id)->trigger();
    pump();
}
QPoint P(double x, double y)
{
    return g_canvas->imageToWidget(QPointF(x, y)).toPoint();
}
void send(QEvent::Type t, const QPoint& at, Qt::MouseButton b, Qt::MouseButtons bs)
{
    QMouseEvent ev(t, QPointF(at), g_canvas->mapToGlobal(QPointF(at)), b, bs, Qt::NoModifier);
    QCoreApplication::sendEvent(g_canvas, &ev);
}
void drag(QPointF a, QPointF b, int steps = 14)
{
    send(QEvent::MouseButtonPress, P(a.x(), a.y()), Qt::LeftButton, Qt::LeftButton);
    for (int i = 1; i <= steps; ++i) {
        const double t = double(i) / steps;
        send(QEvent::MouseMove, P(a.x() + (b.x() - a.x()) * t, a.y() + (b.y() - a.y()) * t),
             Qt::NoButton, Qt::LeftButton);
    }
    send(QEvent::MouseButtonRelease, P(b.x(), b.y()), Qt::LeftButton, Qt::NoButton);
}
void click(QPointF a)
{
    const QPoint at = P(a.x(), a.y());
    send(QEvent::MouseButtonPress, at, Qt::LeftButton, Qt::LeftButton);
    send(QEvent::MouseButtonRelease, at, Qt::LeftButton, Qt::NoButton);
}
void setPrimary(pixel_t c)
{
    ToolManager* mgr = g_win->findChild<ToolManager*>();
    mgr->setPrimaryColor(c);
    mgr->setActive(mgr->active());
    pump();
}
} // namespace

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    MainWindow w;
    w.resize(1500, 950);
    w.show();
    for (int i = 0; i < 40; ++i)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    g_win = &w;
    g_canvas = w.findChild<CanvasView*>();

    Document* doc = w.document();
    doc->layers().first()->surface().fill(whitePixel());
    doc->history()->clear();
    g_canvas->zoomToFit();
    pump();

    // A freehand stroke, smooth and round, in a strong colour.
    setPrimary(0xFF1F6FEBu);
    useTool("pencil");
    drag(QPointF(70, 90), QPointF(300, 170), 20);
    drag(QPointF(300, 170), QPointF(120, 260), 20);

    // A line: this is the one that used to be invisible.
    setPrimary(0xFFD7263Du);
    useTool("line");
    drag(QPointF(340, 90), QPointF(560, 230));

    // A filled shape and an outline shape.
    setPrimary(0xFF2EA043u);
    useTool("rectangle");
    drag(QPointF(340, 260), QPointF(470, 370));
    setPrimary(0xFFBF3989u);
    useTool("ellipse");
    drag(QPointF(500, 260), QPointF(630, 370));

    // Polygon, closed with Enter.
    setPrimary(0xFFE08A1Eu);
    useTool("polygon");
    click(QPointF(70, 320));
    click(QPointF(180, 300));
    click(QPointF(220, 420));
    click(QPointF(110, 430));
    QKeyEvent ev(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
    QCoreApplication::sendEvent(g_canvas, &ev);

    // A filled region.
    setPrimary(0xFF00B4D8u);
    useTool("bucket");
    click(QPointF(660, 460));

    // A gradient.
    useTool("gradient");
    drag(QPointF(660, 60), QPointF(760, 200));

    // Eraser over part of the first stroke.
    useTool("eraser");
    drag(QPointF(150, 210), QPointF(240, 230));

    pump(20);
    const QPixmap shot = g_canvas->grab();
    const QString out = QStringLiteral("/tmp/pnq_draw.png");
    if (!shot.save(out)) {
        std::fprintf(stderr, "could not save %s\n", qPrintable(out));
        return 1;
    }
    std::fprintf(stderr, "saved %s (%dx%d)\n", qPrintable(out), shot.width(), shot.height());
    return 0;
}
