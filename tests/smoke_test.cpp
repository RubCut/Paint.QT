// Smoke test: builds the real MainWindow, renders it to PNG and drives a few
// tools programmatically. Run with QT_QPA_PLATFORM=offscreen.
#include "core/Document.h"
#include "core/ImageOps.h"
#include "tools/Tool.h"
#include "tools/ToolManager.h"
#include "ui/CanvasView.h"
#include "ui/PalettePanel.h"
#include "ui/MainWindow.h"

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QDir>
#include <QMenu>
#include <QMenuBar>
#include <QPixmap>
#include <QScreen>
#include <QTimer>
#include <QToolBar>

#include <cmath>

#include <cstdio>

using namespace pnq;

static int g_failures = 0;

static void check(bool ok, const char* what)
{
    std::printf("  [%s] %s\n", ok ? " OK " : "FAIL", what);
    if (!ok)
        ++g_failures;
}

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    const QString outDir = QDir::tempPath();
    std::printf("=== GUI smoke test ===\n");

    // ---------------------------------------------------------- main window
    MainWindow* w = new MainWindow;
    w->resize(1440, 900);
    w->show();
    // Let the layout, docks and painters settle.
    for (int i = 0; i < 40; ++i) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QCoreApplication::sendPostedEvents();
    }
    check(w->document() != nullptr, "document created");
    check(w->document()->layerCount() == 1, "one background layer");
    check(w->document()->width() == 800, "canvas is 800x600");

    // Grab the whole window. The palettes are separate top-level windows now,
    // as in Paint.NET, so a plain widget grab would miss them; the screen grab
    // shows what the user actually sees.
    // Render rather than grab the screen: grabWindow(0) composites the root
    // window, which on X11 can show stale content where the floating palettes
    // are, and it is unavailable on the offscreen platform. The palettes are
    // child widgets now, so render() captures everything the user sees.
    QPixmap shot(w->size());
    shot.fill(Qt::white);
    w->render(&shot);
    if (shot.isNull() || shot.width() <= 1000)
        std::printf("       -> grab null=%d size %dx%d  window %dx%d\n", (int)shot.isNull(), shot.width(), shot.height(), w->width(), w->height());
    check(!shot.isNull() && shot.width() > 1000, "window renders");
    const QString shotPath = outDir + QStringLiteral("/pnq_window.png");
    check(shot.save(shotPath), "window screenshot saved");
    std::printf("       -> %s (%dx%d)\n", qPrintable(shotPath), shot.width(), shot.height());

    // The canvas must not be an empty grey rectangle.
    QImage canvasShot = shot.toImage();
    int nonGrey = 0;
    for (int y = 0; y < canvasShot.height(); y += 7) {
        for (int x = 0; x < canvasShot.width(); x += 7) {
            const QRgb c = canvasShot.pixel(x, y);
            if (qRed(c) != qGreen(c) || qGreen(c) != qBlue(c))
                ++nonGrey;
        }
    }
    check(nonGrey > 50, "canvas has content (toolbars/icons drawn)");

    // ---------------------------------------------------------- the tools
    CanvasView* canvas = w->findChild<CanvasView*>();
    check(canvas != nullptr, "canvas view present");
    check(canvas->document() == w->document(), "canvas bound to document");
    ToolManager* tm = w->findChild<ToolManager*>();
    check(tm != nullptr, "tool manager present");
    check(tm->count() >= 25, "at least 25 tools registered");

    Document* doc = w->document();
    Layer* layer = doc->activeLayer();
    doc->history()->clear();

    // Every tool must be constructible, activatable and survive input events.
    int toolsOk = 0;
    for (Tool* t : tm->tools()) {
        tm->setActive(t);
        QCoreApplication::processEvents();
        const QPoint p(100, 100);
        const QPoint q(160, 140);
        t->mouseDown(p, Qt::LeftButton, Qt::NoModifier);
        t->mouseDrag(QPointF(130, 120), Qt::LeftButton, Qt::NoModifier);
        t->mouseMove(q, Qt::LeftButton, Qt::NoModifier);
        t->mouseUp(q, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::processEvents();
        if (t->id().isEmpty())
            continue;
        ++toolsOk;
    }
    check(toolsOk == tm->count(), "all tools survived a synthetic click");

    // The pencil must actually change pixels and be undoable.
    tm->setActiveById(QStringLiteral("pencil"));
    layer->surface().fill(rgbPixel(255, 255, 255));
    Tool* pencil = tm->active();
    check(pencil != nullptr, "pencil tool found");
    if (pencil) {
        pencil->setPrimaryColor(rgbPixel(255, 0, 0));
        pencil->brush().setSize(9);
        pencil->brush().setHardness(100);
        pencil->brush().setSpacing(10);
        pencil->mouseDown(QPoint(50, 50), Qt::LeftButton, Qt::NoModifier);
        for (int x = 50; x < 200; x += 8)
            pencil->mouseMove(QPoint(x, 50), Qt::LeftButton, Qt::NoModifier);
        pencil->mouseUp(QPoint(200, 50), Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::processEvents();

        const pixel_t p = layer->surface().pixel(120, 50);
        check(getR(p) > 200 && getG(p) < 60, "pencil stroke painted red pixels");
        check(doc->history()->canUndo(), "stroke pushed to history");
        doc->history()->undo();
        QCoreApplication::processEvents();
        check(layer->surface().pixel(120, 50) == rgbPixel(255, 255, 255), "undo restored the pixels");
        doc->history()->redo();
        check(getR(layer->surface().pixel(120, 50)) > 200, "redo re-applied the pixels");
    }

    // The eraser must clear alpha.
    tm->setActiveById(QStringLiteral("eraser"));
    Tool* eraser = tm->active();
    if (eraser) {
        eraser->brush().setSize(15);
        eraser->brush().setHardness(100);
        eraser->mouseDown(QPoint(120, 50), Qt::LeftButton, Qt::NoModifier);
        eraser->mouseUp(QPoint(120, 50), Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::processEvents();
        check(getA(layer->surface().pixel(120, 50)) < 255, "eraser cleared alpha");
    }

    // Fill the layer with a gradient so the screenshot shows something nice.
    doc->history()->clear();
    tm->setActiveById(QStringLiteral("bucket"));
    Tool* bucket = tm->active();
    if (bucket) {
        bucket->setPrimaryColor(rgbPixel(40, 90, 200));
        bucket->mouseDown(QPoint(300, 300), Qt::LeftButton, Qt::NoModifier);
        bucket->mouseUp(QPoint(300, 300), Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::processEvents();
        check(getR(layer->surface().pixel(300, 300)) > 30, "paint bucket filled the layer");
    }

    // A second layer on top, so the Layers pane has something to show.
    Layer* second = doc->addLayer(QStringLiteral("Smoke Layer"));
    Surface s(200, 120);
    s.fill(qPremult(255, 240, 120, 40));
    second->setSurface(s);
    doc->setActiveLayerIndex(1);
    QCoreApplication::processEvents();
    check(doc->layerCount() == 2, "second layer added");

    // ---------------------------------------------------------- canvas render
    canvas->zoomToFit();
    for (int i = 0; i < 20; ++i)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);

    const QImage composite = canvas->compositeImage();
    check(!composite.isNull(), "canvas composite produced");
    check(composite.width() == doc->width(), "composite size matches the document");
    // Layer 2 only covers 200x120 at the origin, so the top left must be the
    // orange fill and the bottom right must show layer 1's blue.
    check(getR(composite.pixel(10, 10)) > 200 && getB(composite.pixel(10, 10)) < 80,
          "layer 2 shows on top of layer 1");
    check(getB(composite.pixel(600, 500)) > 150, "layer 1 visible where layer 2 is absent");

    const QPixmap canvasPix = canvas->grab();
    const QString canvasPath = outDir + QStringLiteral("/pnq_canvas.png");
    check(canvasPix.save(canvasPath), "canvas screenshot saved");
    std::printf("       -> %s\n", qPrintable(canvasPath));

    // ---------------------------------------------------------- menu sanity
    int menuActions = 0;
    for (QAction* a : w->menuBar()->actions())
        menuActions += a->menu() ? a->menu()->actions().size() : 1;
    check(menuActions >= 60, "menus expose 60+ commands");
    check(w->findChildren<QAction*>().size() >= 70, "action table is populated");

    // Tools on the toolbar.
    check(w->findChildren<QToolBar*>().size() >= 2, "tool bars present");

    // The three palettes Paint.NET shows by default must be floating panels
    // inside the window: Wayland refuses to let a client place real top level
    // windows, so a docked palette is the only thing that would work there and
    // it is not what the original looks like.
    {
        int visible = 0;
        for (PalettePanel* p : w->findChildren<PalettePanel*>()) {
            if (!p->isVisible())
                continue;
            ++visible;
            // Inside the window, not spilling out of it.
            const bool inside = p->parentWidget() == w && p->geometry().intersects(w->rect());
            const QString msg = QStringLiteral("panel '%1' is inside the window").arg(p->title());
            check(inside, qPrintable(msg));
        }
        check(visible == 3, "colors, layers and history are floating panels");

        // Dragging a panel has to actually move it. Without WA_OpaquePaintEvent
        // the region it vacates is never invalidated, and the panel leaves a
        // ghost of its old position behind while it is being dragged.
        for (PalettePanel* panel : w->findChildren<PalettePanel*>()) {
            if (panel->isHidden())
                continue;
            // A real press on the title bar followed by moves, the way a user
            // drags it. The offset is taken in the panel's own coordinates, so
            // feeding those straight to the parent used to throw the panel at
            // the parent's origin on the very first move.
            const QPoint pressInPanel(40, 8);
            const QPoint grabGlobal = panel->mapToGlobal(pressInPanel);
            const QPoint parentPress = panel->parentWidget()->mapFromGlobal(grabGlobal);
            auto mouse = [&](QEvent::Type t, const QPoint& inPanel, Qt::MouseButton b,
                             Qt::MouseButtons bs) {
                QMouseEvent ev(t, QPointF(inPanel), panel->mapToGlobal(inPanel), b, bs,
                               Qt::NoModifier);
                QCoreApplication::sendEvent(panel, &ev);
            };
            mouse(QEvent::MouseButtonPress, pressInPanel, Qt::LeftButton, Qt::LeftButton);
            const QPoint start = panel->pos();
            const QPoint step(-10, 6);
            const int steps = 12;
            // Regression guard: the drag offset is measured in the panel's own
            // coordinates, so feeding it to the parent threw the panel at the
            // parent's origin on the very first move.
            mouse(QEvent::MouseMove, pressInPanel + step, Qt::NoButton, Qt::LeftButton);
            QCoreApplication::processEvents(QEventLoop::AllEvents, 4);
            const QPoint afterFirst = panel->pos();
            const QString noThrow = QStringLiteral("panel '%1' does not jump to the origin (%2,%3)")
                                        .arg(panel->title()).arg(afterFirst.x()).arg(afterFirst.y());
            check(afterFirst != QPoint(0, 0)
                      && (afterFirst - start).manhattanLength() <= step.manhattanLength() * 3,
                  qPrintable(noThrow));
            for (int i = 2; i <= steps; ++i) {
                mouse(QEvent::MouseMove, pressInPanel + step * i, Qt::NoButton, Qt::LeftButton);
                QCoreApplication::processEvents(QEventLoop::AllEvents, 3);
            }
            const QPoint end = panel->pos();
            // It must end up on the far side of where the cursor went.
            const bool wentLeft = end.x() < start.x();
            const bool wentDown = end.y() > start.y();
            check(wentLeft && wentDown,
                  qPrintable(QStringLiteral("panel '%1' tracks the cursor: %2,%3 -> %4,%5")
                                 .arg(panel->title()).arg(start.x()).arg(start.y())
                                 .arg(end.x()).arg(end.y())));
            mouse(QEvent::MouseButtonRelease, QPoint(pressInPanel.x() - 120, pressInPanel.y() + 72),
                  Qt::LeftButton, Qt::NoButton);
            QCoreApplication::processEvents(QEventLoop::AllEvents, 8);
            Q_UNUSED(parentPress);
            check(panel->testAttribute(Qt::WA_OpaquePaintEvent),
                  "panel repaints its whole rect so it leaves no ghost");
            // Put it back where the corner pinning wants it and check it landed.
            panel->setPinned(true);
        }
        w->reanchorPalettes();
        for (int i = 0; i < 20; ++i)
            QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
        {
            auto* cv = w->findChild<CanvasView*>();
            const QRect area(cv->mapTo(static_cast<QWidget*>(w), QPoint(0, 0)), cv->size());
            int inCorner = 0;
            for (PalettePanel* p : w->findChildren<PalettePanel*>()) {
                if (p->isHidden())
                    continue;
                const QRect g = p->geometry();
                const bool rightSide = g.center().x() > area.center().x();
                const bool bottomSide = g.center().y() > area.center().y();
                // Within 12 px of the corner it should have snapped to.
                const int dx = rightSide ? area.right() - g.right() : g.left() - area.left();
                const int dy = bottomSide ? area.bottom() - g.bottom() : g.top() - area.top();
                const QString msg = QStringLiteral("panel '%1' is back in its corner (%2,%3)")
                                        .arg(p->title()).arg(dx).arg(dy);
                check(qAbs(dx) <= 12 && qAbs(dy) <= 12, qPrintable(msg));
                if (qAbs(dx) <= 12 && qAbs(dy) <= 12)
                    ++inCorner;
            }
            check(inCorner == 3, "all three palettes snapped back to a corner");
        }
        for (int i = 0; i < 20; ++i)
            QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
    }

    // ------------------------------------------------- window manager dragging
    // While the window manager moves the window the app must not repaint or touch
    // any geometry: doing so makes the window snap back to its previous position,
    // so it flickers between the cursor and the place it started from.
    {
        QVector<QPoint> panelBefore;
        for (PalettePanel* p : w->findChildren<PalettePanel*>())
            panelBefore.append(p->pos());
        for (int i = 0; i < 20; ++i) {
            w->move(w->pos() + QPoint(6, 4));
            QCoreApplication::processEvents(QEventLoop::AllEvents, 2);
        }
        const QPoint last = w->pos();
        for (int i = 0; i < 40; ++i)
            QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        check(w->pos() == last, "the window keeps every position the WM gives it");
        int k = 0, movedPanels = 0;
        for (PalettePanel* p : w->findChildren<PalettePanel*>()) {
            if (p->pos() != panelBefore[k])
                ++movedPanels;
            ++k;
        }
        check(movedPanels == 0, "the palettes are not touched while the window moves");
    }

    // ------------------------------------------------- freehand path smoothing
    // Sparse mouse events used to be joined with straight chords, which cut the
    // corner of every curve. Measure how far the stroke strays inside a true
    // circle and compare against the sagitta a raw polyline would leave.
    {
        const int sz = 300, radius = 120;
        const QPointF centre(sz / 2.0, sz / 2.0);
        auto strokeInwardError = [&](int stepDeg) {
            Document doc(sz, sz);
            Layer* layer = doc.addLayer(QStringLiteral("probe"));
            layer->surface().fill(0x00000000u);

            ToolManager mgr;
            mgr.setActiveById(QStringLiteral("pencil"));
            Tool* t = mgr.active();
            Brush b;
            b.setSize(3);
            b.setHardness(100);
            b.setOpacity(100);
            b.setSpacing(1);
            b.setAntiAliasing(false);
            t->setBrush(b);
            t->setDocument(&doc);
            t->setPrimaryColor(0xFFFFFFFFu);
            t->setSecondaryColor(0xFFFFFFFFu);

            const int steps = qMax(2, int(360.0 / stepDeg));
            for (int i = 0; i <= steps; ++i) {
                const double a = i * stepDeg * M_PI / 180.0;
                const QPoint p(qRound(centre.x() + radius * std::cos(a)),
                               qRound(centre.y() + radius * std::sin(a)));
                if (i == 0)
                    t->mouseDown(p, Qt::LeftButton, Qt::NoModifier);
                else
                    t->mouseMove(p, Qt::LeftButton, Qt::NoModifier);
            }
            t->mouseUp(QPoint(qRound(centre.x() + radius), qRound(centre.y())), Qt::LeftButton,
                       Qt::NoModifier);

            // The innermost painted pixel sits one brush radius inside the
            // centre line, so subtract that baseline before comparing.
            const double baseline = b.size() / 2.0 + 0.5;
            double worst = 0.0;
            const Surface& s = layer->surface();
            for (int y = 0; y < sz; ++y) {
                const pixel_t* row = s.scanLine(y);
                for (int x = 0; x < sz; ++x) {
                    if (getA(row[x]) < 40)
                        continue;
                    const double d = std::hypot(x + 0.5 - centre.x(), y + 0.5 - centre.y());
                    worst = qMax(worst, radius - d);
                }
            }
            return worst - baseline;
        };

        for (int step : { 30, 45, 60 }) {
            const int chords = qMax(2, int(360.0 / step));
            const double polyline = radius * (1.0 - std::cos(M_PI / chords));
            const double actual = strokeInwardError(step);
            check(actual < polyline * 0.85,
                  qPrintable(QStringLiteral("pencil smooths the path at a %1 deg mouse step (%2 px vs %3 px)")
                                 .arg(step)
                                 .arg(actual, 0, 'f', 2)
                                 .arg(polyline, 0, 'f', 2)));
        }
    }

    // ------------------------------------------------------------ zoom keys
    // Pressing + / - walks the zoom table. Resolving the current step wrongly
    // made every zoom read as 3.125%, so a keystroke collapsed the image instead
    // of stepping it. The slider went through setZoom() and never saw it.
    {
        auto* zoomInAction = w->findChild<QAction*>(QStringLiteral("view.zoomin"));
        auto* zoomOutAction = w->findChild<QAction*>(QStringLiteral("view.zoomout"));
        auto* fitAction = w->findChild<QAction*>(QStringLiteral("view.zoomfit"));
        check(zoomInAction && zoomOutAction && fitAction, "zoom actions exist");

        fitAction->trigger();
        for (int i = 0; i < 6; ++i)
            QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
        const double fitted = canvas->zoom();
        check(fitted > 0.05 && fitted < 4.0,
              qPrintable(QStringLiteral("fit lands on a sane zoom (%1)")
                             .arg(fitted, 0, 'f', 3)));

        // From "fit", one press of + must land on the next table step, which is
        // never below the fitted value by more than the step's own width.
        zoomInAction->trigger();
        for (int i = 0; i < 4; ++i)
            QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
        const double afterIn = canvas->zoom();
        check(afterIn > fitted,
              qPrintable(QStringLiteral("Zoom In raises the zoom (%1 -> %2)")
                             .arg(fitted, 0, 'f', 3).arg(afterIn, 0, 'f', 3)));

        // Keep pressing: the zoom must keep climbing through the table, not stall
        // at the bottom and not collapse.
        double previous = afterIn;
        int climbs = 0;
        for (int i = 0; i < 6; ++i) {
            zoomInAction->trigger();
            QCoreApplication::processEvents(QEventLoop::AllEvents, 3);
            if (canvas->zoom() > previous)
                ++climbs;
            previous = canvas->zoom();
        }
        check(climbs == 6,
              qPrintable(QStringLiteral("repeated Zoom In keeps stepping (%1 of 6, ended at %2)")
                             .arg(climbs).arg(previous, 0, 'f', 3)));
        check(previous > afterIn * 1.5,
              qPrintable(QStringLiteral("six steps in reach a visibly larger scale (%1)")
                             .arg(previous, 0, 'f', 3)));

        // And back down, without collapsing either.
        int drops = 0;
        double high = previous;
        for (int i = 0; i < 6; ++i) {
            zoomOutAction->trigger();
            QCoreApplication::processEvents(QEventLoop::AllEvents, 3);
            if (canvas->zoom() < high)
                ++drops;
            high = canvas->zoom();
        }
        check(drops == 6,
              qPrintable(QStringLiteral("repeated Zoom Out keeps stepping (%1 of 6, ended at %2)")
                             .arg(drops).arg(high, 0, 'f', 3)));

        // 100% must be reachable and exact.
        w->findChild<QAction*>(QStringLiteral("view.zoom100"))->trigger();
        for (int i = 0; i < 4; ++i)
            QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
        check(std::abs(canvas->zoom() - 1.0) < 0.0001,
              qPrintable(QStringLiteral("Zoom to 100%% is exact (%1)").arg(canvas->zoom(), 0, 'f', 4)));
        fitAction->trigger();
        for (int i = 0; i < 4; ++i)
            QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
    }

    // --------------------------------------------------- undo of a long stroke
    // A stroke's saved pixels live in the tiles the brush crossed. Recording
    // one bounding rectangle instead also covered the corners a diagonal stroke
    // never touched, and undoing wrote transparent pixels into them.
    {
        Document* doc = w->document();
        Layer* l = doc->activeLayer();
        if (l) {
            Surface flat(doc->width(), doc->height());
            flat.fill(rgbPixel(255, 255, 255));
            l->setSurface(flat);
            QCoreApplication::processEvents();

            ToolManager tools;
            tools.setDocument(doc);
            tools.setActiveById(QStringLiteral("pencil"));
            Tool* pen = tools.active();
            check(pen != nullptr, "pencil available for the undo check");
            if (pen) {
                pen->setPrimaryColor(rgbPixel(10, 10, 200));
                pen->brush().setSize(11);
                pen->brush().setHardness(100);
                const QImage white = QImage(l->surface().toQImageConst().copy());
                const int stepsBefore = doc->history()->count();

                // A long diagonal stroke, whose bounding box is far larger than
                // the stroke itself.
                pen->mouseDown(QPoint(20, 20), Qt::LeftButton, Qt::NoModifier);
                for (int i = 1; i <= 40; ++i)
                    pen->mouseMove(QPoint(20 + 15 * i, 20 + 7 * i), Qt::LeftButton, Qt::NoModifier);
                pen->mouseUp(QPoint(620, 300), Qt::LeftButton, Qt::NoModifier);
                QCoreApplication::processEvents();

                const QImage stroked = QImage(l->surface().toQImageConst().copy());
                int changed = 0;
                for (int y = 0; y < stroked.height(); ++y)
                    for (int x = 0; x < stroked.width(); ++x)
                        if (stroked.pixel(x, y) != white.pixel(x, y))
                            ++changed;
                check(changed > 1000,
                      qPrintable(QStringLiteral("the diagonal stroke painted (%1 px)").arg(changed)));
                check(doc->history()->count() == stepsBefore + 1,
                      "the whole stroke is one undo step");

                doc->history()->undo();
                QCoreApplication::processEvents();
                int lost = 0;
                const QImage undone = QImage(l->surface().toQImageConst().copy());
                for (int y = 0; y < undone.height(); ++y)
                    for (int x = 0; x < undone.width(); ++x)
                        if (undone.pixel(x, y) != white.pixel(x, y))
                            ++lost;
                check(lost == 0,
                      qPrintable(QStringLiteral("undo of a diagonal stroke leaves nothing behind "
                                               "(%1 px differ)").arg(lost)));

                doc->history()->redo();
                QCoreApplication::processEvents();
                int lostRedo = 0;
                const QImage redone = QImage(l->surface().toQImageConst().copy());
                for (int y = 0; y < redone.height(); ++y)
                    for (int x = 0; x < redone.width(); ++x)
                        if (redone.pixel(x, y) != stroked.pixel(x, y))
                            ++lostRedo;
                check(lostRedo == 0,
                      qPrintable(QStringLiteral("redo reproduces the stroke exactly (%1 px differ)")
                                     .arg(lostRedo)));
                doc->history()->undo();
                QCoreApplication::processEvents();
            }
        }
    }

    // ------------------------------------------------------------- clipboard
    // The system clipboard is the source of truth, so an image copied in some
    // other program has to arrive here on Paste, and a copy made here has to
    // be readable by that other program.
    {
        auto* copyAction = w->findChild<QAction*>(QStringLiteral("edit.copy"));
        auto* pasteAction = w->findChild<QAction*>(QStringLiteral("edit.paste"));
        auto* newLayerAction = w->findChild<QAction*>(QStringLiteral("edit.pastenewlayer"));
        check(copyAction && pasteAction && newLayerAction, "clipboard actions exist");

        Document* doc = w->document();
        const int layersBefore = doc->layerCount();

        // Stand in for another application: put an image on the clipboard
        // directly, bypassing anything this window did.
        QImage foreign(24, 18, QImage::Format_ARGB32_Premultiplied);
        foreign.fill(QColor(0, 200, 60, 255));
        QApplication::clipboard()->setImage(foreign);
        for (int i = 0; i < 10; ++i)
            QCoreApplication::processEvents(QEventLoop::AllEvents, 10);

        check(pasteAction->isEnabled(),
              "Paste turns on as soon as another program puts an image on the clipboard");

        // The active layer is a small one left over from an earlier check, so a
        // paste aimed at the cursor would fall outside it. Give it document
        // sized pixels so the landing place is decided by this test only.
        {
            Layer* act = doc->activeLayer();
            Surface full(doc->width(), doc->height());
            full.fill(qPremult(0, 0, 0, 0));
            act->setSurface(full);
        }

        // Start from a clean stack: a push after an undo drops the redo tail, so
        // the action count alone would not prove that the paste was recorded.
        doc->history()->clear();
        pasteAction->trigger();
        for (int i = 0; i < 10; ++i)
            QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        check(doc->history()->count() == 1,
              "pasting a foreign image makes one undoable step");

        // The pasted pixels have to be really in the layer, not just counted.
        const QImage canvasImg = QImage(doc->compositeImage());
        int greenFound = 0;
        for (int y = 0; y < canvasImg.height(); ++y)
            for (int x = 0; x < canvasImg.width(); ++x)
                if (canvasImg.pixelColor(x, y) == QColor(0, 200, 60, 255))
                    ++greenFound;
        check(greenFound > 0,
              qPrintable(QStringLiteral("the foreign image's pixels landed on the canvas (%1 px)")
                             .arg(greenFound)));

        // And the other direction: a copy here must reach the system clipboard.
        copyAction->trigger();
        for (int i = 0; i < 10; ++i)
            QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        const QImage outbound = QApplication::clipboard()->image();
        check(!outbound.isNull(), "copying here leaves an image on the system clipboard");

        // Paste into a new layer, which is what Ctrl+Shift+V does.
        const int layersMid = doc->layerCount();
        newLayerAction->trigger();
        for (int i = 0; i < 10; ++i)
            QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        check(doc->layerCount() == layersMid + 1,
              "paste into a new layer adds exactly one layer");
        // A copy taken from a selection has transparent pixels around it.
        // Pasting it must not erase the layer underneath those pixels.
        {
            Document* d2 = w->document();
            Layer* l2 = d2->activeLayer();
            l2->surface().fill(qRgba(10, 20, 30, 255));
            const int bg = d2->history()->count();

            // 10x10 opaque square on a 20x20 transparent field.
            Surface clipSurf(20, 20);
            clipSurf.fill(qRgba(0, 0, 0, 0));
            for (int y = 5; y < 15; ++y)
                for (int x = 5; x < 15; ++x)
                    clipSurf.setPixel(x, y, qRgba(255, 255, 0, 255));

            ImageOps::pasteIntoLayer(*d2, clipSurf, 30, 30, d2->activeLayerIndex());
            const QImage img = QImage(d2->compositeImage());
            const QColor inSquare(255, 255, 0, 255);
            const QColor untouched(10, 20, 30, 255);
            // (35,35) is inside the opaque square; (31,31) is in the transparent
            // margin and must still show the background.
            check(img.pixelColor(35, 35) == inSquare
                      && img.pixelColor(31, 31) == untouched,
                  "pasting an image with transparent margins keeps the pixels underneath");
            check(d2->history()->count() == bg + 1, "the clipped paste is one undo step");

            // Undo has to put the background back everywhere.
            d2->history()->undo();
            check(QImage(d2->compositeImage()).pixelColor(35, 35) == untouched,
                  "undoing the paste restores the layer exactly");
            d2->history()->redo();

            // A layer can be smaller than the document. A paste that misses it
            // entirely changed nothing, so it must not leave an undo step that
            // pretends otherwise.
            Layer* small = d2->addLayer(QStringLiteral("small"));
            Surface tiny(20, 20);
            tiny.fill(qPremult(255, 0, 0, 255));
            small->setSurface(tiny);
            const int beforeMiss = d2->history()->count();
            ImageOps::pasteIntoLayer(*d2, clipSurf, 400, 400, d2->activeLayerIndex());
            check(d2->history()->count() == beforeMiss,
                  "a paste that lands outside the layer records no undo step");
        }

        Q_UNUSED(layersBefore);
    }

    std::printf("=== %s (%d failures) ===\n", g_failures == 0 ? "ALL PASSED" : "FAILURES", g_failures);
    delete w;
    return g_failures == 0 ? 0 : 1;
}
