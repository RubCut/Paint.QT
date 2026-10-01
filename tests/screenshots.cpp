// Renders the application screenshots that go into the AppStream metainfo.
//
// Flathub requires screenshots, and `appstreamcli compose` refuses to produce a
// catalogue from a component that has none. They are produced from the running
// window rather than committed as opaque binaries, so they cannot drift from the
// interface they show.
//
// Usage: pnq_screenshots <output directory>
#include "core/Document.h"
#include "tools/Tool.h"
#include "tools/ToolManager.h"
#include "ui/CanvasView.h"
#include "ui/MainWindow.h"

#include <QAction>
#include <QApplication>
#include <QDir>
#include <QImage>
#include <QPainter>

#include <cstdio>

using namespace pnq;

namespace {

/// Sends a press, a few moves and a release, the way a hand does it.
void stroke(MainWindow* w, const QPointF& from, const QPointF& to)
{
    CanvasView* view = w->findChild<CanvasView*>();
    ToolManager* mgr = w->findChild<ToolManager*>();
    if (!view || !mgr || !mgr->active())
        return;
    Tool* t = mgr->active();
    const QPoint a(int(from.x()), int(from.y()));
    const QPoint b(int(to.x()), int(to.y()));
    t->mouseDown(a, Qt::LeftButton, Qt::NoModifier);
    for (int i = 1; i <= 10; ++i)
        t->mouseMove(a + (b - a) * i / 10, Qt::LeftButton, Qt::NoModifier);
    t->mouseUp(b, Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::processEvents();
}

void pump()
{
    for (int i = 0; i < 25; ++i)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
}

} // namespace

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s <output dir>\n", argv[0]);
        return 2;
    }
    const QString outDir = QString::fromLocal8Bit(argv[1]);
    QDir().mkpath(outDir);

    MainWindow* w = new MainWindow;
    w->resize(1440, 900);
    w->show();
    pump();

    // Pin the light theme. The application otherwise follows the desktop, so the
    // three shots would not match each other whenever the screenshots were taken
    // on a system set to dark.
    if (QAction* light = w->findChild<QAction*>(QStringLiteral("view.theme.light"))) {
        light->trigger();
        pump();
    }

    auto shot = [&](const QString& name) {
        pump();
        QImage img(w->size(), QImage::Format_ARGB32_Premultiplied);
        img.fill(Qt::white);
        w->render(&img);
        const QString path = QStringLiteral("%1/%2").arg(outDir, name);
        if (!img.save(path)) {
            std::fprintf(stderr, "could not write %s\n", qPrintable(path));
            return false;
        }
        std::printf("wrote %s (%dx%d)\n", qPrintable(path), img.width(), img.height());
        return true;
    };

    bool ok = true;

    // 1. The window as it opens.
    ok = shot(QStringLiteral("01-window.png")) && ok;

    // 2. Some drawing, so the canvas is not blank in the store listing.
    {
        Document* doc = w->document();
        Layer* layer = doc->activeLayer();
        if (layer) {
            Surface flat(doc->width(), doc->height());
            flat.fill(rgbPixel(252, 252, 252));
            layer->setSurface(flat);
            doc->history()->clear();
        }
        ToolManager* mgr = w->findChild<ToolManager*>();
        if (mgr) {
            mgr->setActiveById(QStringLiteral("brush"));
            if (Tool* t = mgr->active()) {
                t->setPrimaryColor(rgbPixel(40, 110, 200));
                t->brush().setSize(26);
                t->brush().setHardness(70);
                stroke(w, QPointF(150, 320), QPointF(560, 210));
                t->setPrimaryColor(rgbPixel(220, 60, 90));
                t->brush().setSize(14);
                stroke(w, QPointF(200, 420), QPointF(620, 400));
                mgr->setActiveById(QStringLiteral("pencil"));
            }
        }
        ok = shot(QStringLiteral("02-drawing.png")) && ok;
    }

    // 3. A second layer, which is what the Layers palette is for.
    {
        Document* doc = w->document();
        Layer* top = doc->addLayer(QStringLiteral("Shapes"));
        Surface s(doc->width(), doc->height());
        s.fill(qPremult(0, 0, 0, 0));
        top->setSurface(s);
        doc->setActiveLayerIndex(doc->layerCount() - 1);
        ToolManager* mgr = w->findChild<ToolManager*>();
        if (mgr) {
            mgr->setActiveById(QStringLiteral("ellipse"));
            if (Tool* t = mgr->active()) {
                t->setPrimaryColor(rgbPixel(240, 160, 40));
                t->mouseDown(QPoint(700, 250), Qt::LeftButton, Qt::NoModifier);
                t->mouseMove(QPoint(900, 420), Qt::LeftButton, Qt::NoModifier);
                t->mouseUp(QPoint(900, 420), Qt::LeftButton, Qt::NoModifier);
            }
        }
        ok = shot(QStringLiteral("03-layers.png")) && ok;
    }

    delete w;
    return ok ? 0 : 1;
}