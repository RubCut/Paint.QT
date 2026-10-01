// Regression probe: drive the pencil through real Qt mouse events on a
// CanvasView and check that a drag paints a curved stroke rather than a single
// straight chord from press to release.
#include "core/Document.h"
#include "io/PdnFile.h"
#include "ui/CanvasView.h"
#include "ui/MainWindow.h"

#include <QApplication>
#include <QMouseEvent>
#include <cmath>
#include <cstdio>

using namespace pnq;

int main(int argc, char** argv)
{
    QApplication app(argc, argv);

    const int sz = 400, radius = 150;
    const QPointF centre(sz / 2.0, sz / 2.0);

    MainWindow w;
    w.resize(1200, 900);
    w.show();
    for (int i = 0; i < 40; ++i)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);

    auto* canvas = w.findChild<CanvasView*>();
    std::printf("canvas %dx%d  zoom %.3f  scroll %.0f,%.0f\n", canvas->width(), canvas->height(),
                canvas->zoom(), 0.0, 0.0);

    // Make sure the whole image is on screen at 1:1 so widget coords map simply.
    canvas->setZoom(1.0);
    const QPointF origin = canvas->imageToWidget(QPointF(0, 0));
    std::printf("image (0,0) sits at widget %.0f,%.0f\n", origin.x(), origin.y());

    auto send = [&](QEvent::Type type, const QPoint& at, Qt::MouseButton btn,
                    Qt::MouseButtons buttons, Qt::KeyboardModifiers mods) {
        QMouseEvent ev(type, QPointF(at), canvas->mapToGlobal(QPointF(at)), btn, buttons, mods);
        QCoreApplication::sendEvent(canvas, &ev);
    };

    QPoint prev;
    for (int i = 0; i <= 40; ++i) {
        const double a = i * 9.0 * M_PI / 180.0; // 40 chords over 360 degrees
        const QPointF img(centre.x() + radius * std::cos(a), centre.y() + radius * std::sin(a));
        const QPoint at = canvas->imageToWidget(img).toPoint();
        if (i == 0) {
            send(QEvent::MouseButtonPress, at, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
            prev = at;
            continue;
        }
        send(QEvent::MouseMove, at, Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
        prev = at;
    }
    send(QEvent::MouseButtonRelease, prev, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    for (int i = 0; i < 20; ++i)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);

    // Inspect the document's layer, not the composited widget: the theme's
    // dark canvas backdrop would otherwise be counted as paint.
    Document* doc = w.document();
    Layer* layer = doc->layers().isEmpty() ? nullptr : doc->layers().first();
    if (!layer) {
        std::printf("no layer to inspect\n");
        return 1;
    }
    const Surface& s = layer->surface();
    int painted = 0;
    double worstInward = 0.0;
    double worstOutward = 0.0;
    for (int y = 0; y < s.height(); ++y) {
        const pixel_t* row = s.scanLine(y);
        for (int x = 0; x < s.width(); ++x) {
            // The default background is opaque white, the brush paints black, so
            // look for dark pixels rather than for any coverage.
            if (getA(row[x]) < 40)
                continue;
            if (getR(row[x]) > 100 || getG(row[x]) > 100 || getB(row[x]) > 100)
                continue;
            ++painted;
            const double d = std::hypot(x + 0.5 - centre.x(), y + 0.5 - centre.y());
            worstInward = qMax(worstInward, radius - d);
            worstOutward = qMax(worstOutward, d - radius);
        }
    }
    // A single straight chord from press to release would leave a long run of
    // pixels far inside the circle; a proper arc stays on the radius (plus the
    // brush width).
    const double brush = 19.0 / 2.0 + 1.0;
    std::printf("painted pixels      : %d\n", painted);
    std::printf("worst inward error : %.2f px (brush radius %.1f)\n", worstInward, brush);
    std::printf("worst outward error: %.2f px\n", worstOutward);
    // 40 chords would only sag 0.46 px, so anything near the brush width means
    // the stroke followed the true arc.
    const double sagitta = radius * (1.0 - std::cos(M_PI / 40));
    std::printf("40-chord polyline   : %.2f px\n", sagitta);
    bool ok = painted > 200 && worstInward - brush < 1.5;
    std::printf("verdict: %s\n", ok ? "SMOOTH" : "BROKEN");

    // ---- undo must put the pixels back -------------------------------------
    if (!doc || !doc->history())
        return ok ? 0 : 1;
    const int entries = doc->history()->count();
    std::printf("\nundo stack holds %d entries\n", entries);
    if (entries < 1) {
        std::printf("no undo entry after a drag: BROKEN\n");
        return 1;
    }
    doc->history()->undo();
    for (int i = 0; i < 12; ++i)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
    {
        const Surface& u = doc->layers().first()->surface();
        int left = 0;
        for (int y = 0; y < u.height(); ++y) {
            const pixel_t* row = u.scanLine(y);
            for (int x = 0; x < u.width(); ++x)
                // Opaque and dark. A transparent pixel reads as 0,0,0 and the
                // white background reads as 255, so both must be excluded.
                if (getA(row[x]) > 40 && getR(row[x]) <= 100)
                    ++left;
        }
        std::printf("after undo, non-white pixels = %d\n", left);
        if (left != 0) {
            std::printf("undo did not restore the canvas: BROKEN\n");
            return 1;
        }
    }
    doc->history()->redo();
    for (int i = 0; i < 12; ++i)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
    {
        const Surface& r2 = doc->layers().first()->surface();
        int back = 0;
        for (int y = 0; y < r2.height(); ++y) {
            const pixel_t* row = r2.scanLine(y);
            for (int x = 0; x < r2.width(); ++x)
                if (getA(row[x]) > 40 && getR(row[x]) <= 100)
                    ++back;
        }
        std::printf("after redo, non-white pixels = %d\n", back);
        if (back != painted) {
            std::printf("redo did not restore the stroke: BROKEN\n");
            return 1;
        }
    }
    std::printf("undo/redo round trip: ok\n");

    // ---- .pdn must survive a save and reopen unchanged ---------------------
    {
        const QString path = QStringLiteral("/tmp/pnq_roundtrip.pdn");
        QString err;
        if (!PdnFile::save(*doc, path, &err)) {
            std::printf("pdn save failed: %s\n", qPrintable(err));
            return 1;
        }
        Document* back = PdnFile::load(path, &err);
        if (!back) {
            std::printf("pdn load failed: %s\n", qPrintable(err));
            return 1;
        }
        int diff = 0;
        if (back->width() != doc->width() || back->height() != doc->height()
            || back->layerCount() != doc->layerCount()) {
            std::printf("pdn shape changed: %dx%d/%d -> %dx%d/%d\n", doc->width(), doc->height(),
                        doc->layerCount(), back->width(), back->height(), back->layerCount());
            return 1;
        }
        for (int i = 0; i < doc->layerCount(); ++i) {
            const Surface& a = doc->layerAt(i)->surface();
            const Surface& b = back->layerAt(i)->surface();
            for (int y = 0; y < a.height(); ++y) {
                const pixel_t* ra = a.scanLine(y);
                const pixel_t* rb = b.scanLine(y);
                for (int x = 0; x < a.width(); ++x)
                    if (ra[x] != rb[x])
                        ++diff;
            }
        }
        std::printf("pdn round trip: %d differing pixels\n", diff);
        if (diff != 0)
            return 1;
        delete back;
    }
    return 0;
}
