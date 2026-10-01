// End-to-end audit: drives every tool with real Qt mouse events on a
// CanvasView and reports whether it actually did its job.
//
// A tool that "does nothing" on a blank white canvas may be perfectly correct
// (blur, smudge, flip have nothing to work on), so cases that operate on
// existing pixels paint a fixture first and compare only the tool's effect.
// Each case starts from a blank layer with a cleared history and a reset view.
#include "core/Document.h"
#include "core/Selection.h"
#include "tools/ToolManager.h"
#include "ui/CanvasView.h"
#include "ui/MainWindow.h"
#include "ui/ToolOptions.h"

#include <QAction>
#include <QApplication>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPushButton>
#include <QSpinBox>
#include <QWheelEvent>
#include <cstdio>
#include <functional>
#include <string>

using namespace pnq;

namespace {

int g_pass = 0;
int g_fail = 0;

struct Snapshot
{
    QVector<quint32> px;

    void grab(const Document* doc)
    {
        const int w = doc->width(), h = doc->height();
        px.resize(w * h);
        for (int y = 0; y < h; ++y) {
            const pixel_t* row = doc->layers().first()->surface().scanLine(y);
            for (int x = 0; x < w; ++x)
                px[y * w + x] = row[x];
        }
    }
    int changed(const Snapshot& o) const
    {
        int n = 0;
        for (int i = 0; i < px.size() && i < o.px.size(); ++i)
            if (px[i] != o.px[i])
                ++n;
        return n;
    }
};

MainWindow* g_win = nullptr;
CanvasView* g_canvas = nullptr;

void pump(int n = 6)
{
    for (int i = 0; i < n; ++i)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
}

QAction* actionById(const QString& id)
{
    for (QAction* a : g_win->findChildren<QAction*>())
        if (a->objectName() == id)
            return a;
    return nullptr;
}

void send(QEvent::Type type, const QPoint& at, Qt::MouseButton btn, Qt::MouseButtons buttons,
          Qt::KeyboardModifiers mods)
{
    QMouseEvent ev(type, QPointF(at), g_canvas->mapToGlobal(QPointF(at)), btn, buttons, mods);
    QCoreApplication::sendEvent(g_canvas, &ev);
}

QPoint imgPos(double ix, double iy)
{
    return g_canvas->imageToWidget(QPointF(ix, iy)).toPoint();
}

void drag(const QPointF& from, const QPointF& to, int steps = 12,
          Qt::MouseButtons held = Qt::LeftButton,
          Qt::KeyboardModifiers mods = Qt::NoModifier)
{
    send(QEvent::MouseButtonPress, imgPos(from.x(), from.y()), Qt::LeftButton, Qt::LeftButton, mods);
    for (int i = 1; i <= steps; ++i) {
        const double t = double(i) / steps;
        send(QEvent::MouseMove,
             imgPos(from.x() + (to.x() - from.x()) * t, from.y() + (to.y() - from.y()) * t),
             Qt::NoButton, held, mods);
    }
    send(QEvent::MouseButtonRelease, imgPos(to.x(), to.y()), Qt::LeftButton, Qt::NoButton, mods);
}

void click(const QPointF& p, Qt::MouseButton b = Qt::LeftButton,
           Qt::KeyboardModifiers mods = Qt::NoModifier)
{
    const QPoint at = imgPos(p.x(), p.y());
    send(QEvent::MouseButtonPress, at, b, b, mods);
    send(QEvent::MouseButtonRelease, at, b, Qt::NoButton, mods);
}

void pressKey(int k)
{
    QKeyEvent ev(QEvent::KeyPress, k, Qt::NoModifier);
    QCoreApplication::sendEvent(g_canvas, &ev);
}

void setPrimaryColour(pixel_t c)
{
    ToolManager* mgr = g_win->findChild<ToolManager*>();
    if (mgr) {
        mgr->setPrimaryColor(c);
        mgr->setActive(mgr->active());
    }
}

/// Sets a numeric option on the active tool through its real options widget,
/// which also proves the option is wired to the tool.
/// Sets a numeric option on a tool through its real options widget, which also
/// proves the option is wired up. The label and its spin box share a row widget,
/// so the spin is found inside the label's parent rather than by document order.
bool setToolOptionSpin(const QString& toolId, const QString& label, int value)
{
    ToolManager* mgr = g_win->findChild<ToolManager*>();
    if (!mgr)
        return false;
    mgr->setActiveById(toolId);
    pump();
    Tool* t = mgr->active();
    QWidget* page = t ? t->optionsWidget() : nullptr;
    if (!page)
        return false;
    for (QLabel* lb : page->findChildren<QLabel*>()) {
        if (lb->text().remove(QLatin1Char(':')).trimmed() != label)
            continue;
        QWidget* row = lb->parentWidget();
        if (!row)
            continue;
        for (QSpinBox* sb : row->findChildren<QSpinBox*>()) {
            sb->setValue(value);
            pump();
            return sb->value() == value;
        }
    }
    return false;
}

/// The Finish button lives at the end of the tool options strip.
QPushButton* findFinishButton()
{
    ToolOptionsBar* bar = g_win->findChild<ToolOptionsBar*>();
    if (!bar)
        return nullptr;
    for (QPushButton* b : bar->findChildren<QPushButton*>())
        if (b->text() == QObject::tr("Finish"))
            return b;
    return nullptr;
}

void useTool(const QString& id)
{
    if (QAction* a = actionById(QStringLiteral("tool.") + id)) {
        a->trigger();
        pump();
    }
}

void resetDocument()
{
    Document* d = g_win->document();
    if (d) {
        d->layers().first()->surface().fill(whitePixel());
        if (d->history())
            d->history()->clear();
        d->setSelection(Selection());
    }
    g_canvas->zoomToFit();
    pump();
}

/// Paints a few dark strokes and a filled block so that filters, transforms and
/// selection tools have something to act on.
void paintFixture()
{
    useTool(QStringLiteral("pencil"));
    drag(QPointF(90, 90), QPointF(300, 140), 8);
    drag(QPointF(90, 170), QPointF(260, 250), 8);
    drag(QPointF(330, 90), QPointF(330, 260), 8);
    click(QPointF(430, 100));
    click(QPointF(460, 130));
    click(QPointF(490, 170));
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

    struct Case
    {
        QString label;
        QString toolId;     // empty for composite cases
        bool fixture;       // paint a fixture first?
        std::function<void()> act;
        enum Expect { Paint, NoPixelChange, ViewOnly } expect;
    };
    QVector<Case> cases;
    auto add = [&](const char* label, const char* tool, bool fixture, Case::Expect expect,
                   std::function<void()> act) {
        cases.append({ QString::fromLatin1(label), QString::fromLatin1(tool), fixture,
                       std::move(act), expect });
    };

    // ---- freehand ----------------------------------------------------------
    for (const char* id : { "pencil", "brush" })
        add(id, id, false, Case::Paint, [&] { drag(QPointF(120, 120), QPointF(320, 220)); });
    for (const char* id : { "eraser" })
        add(id, id, true, Case::Paint, [&] { drag(QPointF(100, 100), QPointF(300, 250)); });
    for (const char* id : { "blur", "smudge", "dodgeburn" })
        add(id, id, true, Case::Paint, [&] { drag(QPointF(100, 100), QPointF(300, 250)); });
    // Recolor replaces the colour under the cursor with the primary colour, so
    // the primary has to differ from the black fixture or the stroke is a no-op.
    // Recolor shifts every pixel under the brush, so a non-zero shift is needed
    // for the stroke to be visible. The fixture is black, and a hue or
    // saturation shift does nothing to a fully desaturated colour, so drive the
    // luminosity control through the real options widget.
    add("recolor", "recolor", true, Case::Paint, [&] {
        const bool ok = setToolOptionSpin(QStringLiteral("recolor"), QStringLiteral("Luminosity"), 70);
        drag(QPointF(100, 100), QPointF(300, 250));
        if (!ok)
            std::fprintf(stderr, "[recolor] could not find the Luminosity option\n");
    });

    // ---- shapes and lines --------------------------------------------------
    add("line", "line", false, Case::Paint, [&] { drag(QPointF(150, 150), QPointF(330, 290)); });
    for (const char* id : { "rectangle", "ellipse" })
        add(id, id, false, Case::Paint, [&] { drag(QPointF(150, 150), QPointF(330, 290)); });
    // Multi-click tools: they collect points and only paint once closed, so a
    // plain drag would leave them with an unfinished preview.
    add("polygon", "polygon", false, Case::Paint, [&] {
        click(QPointF(150, 150));
        click(QPointF(330, 170));
        click(QPointF(300, 300));
        click(QPointF(160, 280));
        pressKey(Qt::Key_Return);
    });
    add("freeform", "freeform", false, Case::Paint, [&] { drag(QPointF(150, 150), QPointF(330, 290)); });
    // The Tool Options Finish button has to commit a shape just like Enter does,
    // and it must be dead again for tools that hold no session.
    add("polygon-finish-button", "", false, Case::Paint, [&] {
        useTool(QStringLiteral("polygon"));
        QPushButton* fin = findFinishButton();
        if (!fin || fin->isEnabled())
            return; // the button must start disabled
        click(QPointF(110, 110));
        click(QPointF(250, 130));
        click(QPointF(230, 270));
        if (!fin->isEnabled())
            return; // and light up once points are down
        fin->click();
        pump();
    });
    add("finish-disabled-for-pencil", "", false, Case::NoPixelChange, [&] {
        useTool(QStringLiteral("pencil"));
        QPushButton* fin = findFinishButton();
        if (fin && fin->isEnabled())
            click(QPointF(200, 200));
    });
    add("curve", "curve", false, Case::Paint, [&] {
        click(QPointF(140, 260));
        click(QPointF(240, 150));
        click(QPointF(360, 260));
        pressKey(Qt::Key_Return);
    });

    // ---- selection: ants only, so pixels must not move ---------------------
    for (const char* id : { "selectrect", "selectellipse", "lasso" })
        add(id, id, false, Case::NoPixelChange, [&] { drag(QPointF(100, 100), QPointF(250, 200)); });
    add("selecttransparent", "selecttransparent", false, Case::NoPixelChange,
        [&] { click(QPointF(60, 60)); });
    add("wand", "wand", true, Case::NoPixelChange, [&] { click(QPointF(120, 110)); });
    add("move-selected", "move", true, Case::Paint, [&] {
        drag(QPointF(100, 90), QPointF(300, 260));
        drag(QPointF(200, 175), QPointF(230, 205));
    });
    add("erase-under-selection", "", true, Case::Paint, [&] {
        useTool(QStringLiteral("selectrect"));
        drag(QPointF(90, 80), QPointF(320, 280));
        // The selection stays active; now actually erase inside it.
        useTool(QStringLiteral("eraser"));
        drag(QPointF(100, 90), QPointF(300, 260));
    });

    // ---- fill and pick -----------------------------------------------------
    add("bucket", "bucket", false, Case::Paint, [&] { click(QPointF(60, 60)); });
    add("gradient", "gradient", false, Case::Paint, [&] { drag(QPointF(120, 120), QPointF(320, 320)); });
    add("picker", "picker", true, Case::NoPixelChange, [&] { click(QPointF(120, 110)); });

    // ---- transform (need pixels to move) -----------------------------------
    add("transform", "transform", true, Case::Paint, [&] { drag(QPointF(180, 180), QPointF(300, 300)); });
    add("rotate", "rotate", true, Case::Paint, [&] { drag(QPointF(200, 200), QPointF(260, 260)); });
    add("flip", "flip", true, Case::Paint, [&] { drag(QPointF(180, 180), QPointF(300, 300)); });

    // ---- view --------------------------------------------------------------
    add("zoom", "zoom", false, Case::ViewOnly, [&] {
        for (int i = 0; i < 3; ++i) {
            QWheelEvent we(QPointF(300, 300), QPointF(300, 300), QPoint(0, 0), QPoint(0, 120),
                           Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
            QCoreApplication::sendEvent(g_canvas, &we);
        }
    });
    add("pan", "pan", false, Case::ViewOnly, [&] {
        send(QEvent::MouseButtonPress, QPoint(300, 300), Qt::MiddleButton, Qt::MiddleButton,
             Qt::NoModifier);
        send(QEvent::MouseMove, QPoint(320, 320), Qt::NoButton, Qt::MiddleButton, Qt::NoModifier);
        send(QEvent::MouseButtonRelease, QPoint(320, 320), Qt::MiddleButton, Qt::NoButton,
             Qt::NoModifier);
    });

    // ---- text --------------------------------------------------------------
    add("text", "text", false, Case::ViewOnly, [&] { click(QPointF(200, 200)); });

    // Each stroke must be its own undo step: folding two strokes into one entry
    // makes a single Ctrl+Z throw away the whole drawing.
    {
        resetDocument();
        useTool(QStringLiteral("pencil"));
        drag(QPointF(100, 100), QPointF(280, 150));
        pump();
        const int afterFirst = w.document()->history()->count();
        drag(QPointF(120, 200), QPointF(300, 250));
        pump();
        const int afterSecond = w.document()->history()->count();
        std::printf("%-24s %9s %6d  %s\n", "undo-per-stroke", "-", afterSecond,
                    afterFirst == 1 && afterSecond == 2 ? "ok" : "BROKEN (strokes merged)");
        if (afterFirst == 1 && afterSecond == 2) {
            ++g_pass;
        } else {
            ++g_fail;
            std::printf("       ^ expected 1 then 2 undo entries, got %d then %d\n", afterFirst,
                        afterSecond);
        }
    }

    std::printf("%-24s %9s %6s  %s\n", "tool", "pixels", "undo", "result");
    std::printf("%s\n", std::string(56, '-').c_str());

    for (const Case& c : cases) {
        resetDocument();
        if (c.fixture)
            paintFixture();
        if (c.toolId.isEmpty()) {
            // composite case drives its own tools
        } else {
            if (!actionById(QStringLiteral("tool.") + c.toolId)) {
                std::printf("%-24s %9s %6s  NO ACTION\n", qPrintable(c.label), "-", "-");
                ++g_fail;
                continue;
            }
            useTool(c.toolId);
        }

        Document* doc = w.document();
        if (doc->history())
            doc->history()->clear();
        Snapshot before;
        before.grab(doc);

        c.act();
        pump(12);

        Snapshot after;
        after.grab(doc);
        const int changed = after.changed(before);
        const int entries = doc->history() ? doc->history()->count() : 0;

        const char* verdict = "ok";
        switch (c.expect) {
        case Case::Paint:
            if (changed <= 20)
                verdict = "BROKEN (no paint)";
            else if (entries < 1)
                verdict = "BROKEN (no undo)";
            break;
        case Case::NoPixelChange:
        case Case::ViewOnly:
            if (changed > 0)
                verdict = "BROKEN (pixels moved)";
            break;
        }
        const bool ok = std::string(verdict) == "ok";
        ok ? ++g_pass : ++g_fail;
        std::printf("%-24s %9d %6d  %s\n", qPrintable(c.label), changed, entries, verdict);
    }

    std::printf("\n%d ok, %d broken\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
