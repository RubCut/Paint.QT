// Icon audit: every tool and every command must have its own drawing, the
// application mark must exist at every size a desktop asks for, and a contact
// sheet is written so the artwork can be looked at rather than only asserted.
//
// Two tools that fall through to the same placeholder look identical, which is
// exactly what a missing drawing looks like from the outside. So this test
// compares the rendered pixels rather than trusting that an id was handled.
#include "resources/Icons.h"
#include "tools/ToolManager.h"
#include "ui/MainWindow.h"

#include <QAction>
#include <QApplication>
#include <QDir>
#include <QImage>
#include <QPainter>
#include <QSet>
#include <QToolBar>
#include <QSvgRenderer>
#include <QFile>

#include <cstdlib>

#include <cstdio>

using namespace pnq;

static int g_failures = 0;

static void check(bool ok, const QString& what)
{
    std::printf("  [%s] %s\n", ok ? " OK " : "FAIL", qPrintable(what));
    if (!ok)
        ++g_failures;
}

/// True when the pixmap has some ink in it, i.e. it is not an empty square.
static bool hasInk(const QImage& img)
{
    for (int y = 0; y < img.height(); ++y)
        for (int x = 0; x < img.width(); ++x)
            if (qAlpha(img.pixel(x, y)) > 8)
                return true;
    return false;
}

/// Every command verb a menu can ask for, not only the ten that sit on the
/// command bar, so a missing glyph turns up here rather than as a blank item.
static QStringList allCommandVerbs()
{
    return { "new", "open", "save", "saveas", "print", "cut", "copy", "paste", "undo", "redo",
             "selectall", "crop", "trim", "resize", "rotate90cw", "rotate90ccw", "flipH", "flipV",
             "zoom", "clear", "about", "help", "exit" };
}

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    std::printf("=== icon audit ===\n");

    // ------------------------------------------------------- application mark
    {
        const QIcon icon = Icons::app();
        const QList<QSize> sizes = icon.availableSizes();
        check(sizes.size() >= 8,
              QStringLiteral("the application mark carries every size (%1)").arg(sizes.size()));

        // A desktop asking for a size must get that size, never an upscale of a
        // smaller bitmap, which is what made the taskbar entry look soft.
        QSet<int> want{ 16, 22, 24, 32, 48, 64, 128, 256 };
        QStringList upscaled;
        for (const int s : std::as_const(want)) {
            const QPixmap pm = icon.pixmap(s, s);
            if (pm.width() != s || pm.height() != s)
                upscaled << QStringLiteral("%1->%2x%3").arg(s).arg(pm.width()).arg(pm.height());
        }
        check(upscaled.isEmpty(),
              upscaled.isEmpty() ? QStringLiteral("every requested size is served exactly")
                                 : QStringLiteral("upscaled instead of redrawn: %1")
                                       .arg(upscaled.join(QStringLiteral(", "))));

        // The mark has to survive being shrunk all the way down: at 16 px it is
        // still mostly the frame plus the brush, not a grey smudge.
        const QImage small = icon.pixmap(16, 16).toImage();
        int opaque = 0;
        for (int y = 0; y < small.height(); ++y)
            for (int x = 0; x < small.width(); ++x)
                if (qAlpha(small.pixel(x, y)) > 200)
                    ++opaque;
        check(opaque >= 16 * 16 / 4,
              QStringLiteral("the mark still reads at 16 px (%1 of 256 px covered)")
                  .arg(opaque));

        // The packaged SVG and the icon the window draws must be the same mark.
        // They are two separate files, so nothing stops them drifting apart
        // quietly; here they are rendered and compared.
        const QString svgPath = QStringLiteral(PNQ_SOURCE_DIR "/packaging/icons/paint-qt.svg");
        if (!QFile::exists(svgPath)) {
            check(false, QStringLiteral("packaged SVG is missing (%1)").arg(svgPath));
        } else {
            QSvgRenderer renderer(svgPath);
            check(renderer.isValid(),
                  QStringLiteral("packaged SVG parses (%1)").arg(svgPath));
            if (renderer.isValid()) {
                // A mean absolute error per channel. Anti-aliasing between two
                // different rasterisers shifts edges by a pixel, so the bar is set
                // where "the same drawing" ends and "a different logo" begins.
                double worst = 0.0;
                int worstAt = 0;
                for (const int s : { 64, 128, 256 }) {
                    QImage fromFile(s, s, QImage::Format_ARGB32_Premultiplied);
                    fromFile.fill(Qt::transparent);
                    QPainter fp(&fromFile);
                    renderer.render(&fp, QRectF(0, 0, s, s));
                    fp.end();

                    const QImage drawn = icon.pixmap(s, s).toImage()
                                             .convertToFormat(QImage::Format_ARGB32_Premultiplied);
                    double total = 0;
                    for (int y = 0; y < s; ++y)
                        for (int x = 0; x < s; ++x) {
                            const QRgb a = fromFile.pixel(x, y);
                            const QRgb b = drawn.pixel(x, y);
                            total += std::abs(qRed(a) - qRed(b)) + std::abs(qGreen(a) - qGreen(b))
                                   + std::abs(qBlue(a) - qBlue(b))
                                   + std::abs(qAlpha(a) - qAlpha(b));
                        }
                    const double mae = total / (double(s) * s * 4.0);
                    if (mae > worst) {
                        worst = mae;
                        worstAt = s;
                    }
                }
                check(worst < 12.0,
                      QStringLiteral("the SVG and the drawn mark agree (worst mean error %1 at %2 px)")
                          .arg(worst, 0, 'f', 2)
                          .arg(worstAt));
            }
        }
    }

    // -------------------------------------------------- tools and commands
    ToolManager tools;
    QStringList toolIds;
    for (Tool* t : tools.tools())
        toolIds << t->id();

    // What the user actually sees: the icons on the real toolbars. Reading them
    // off the widgets means the list cannot drift away from the code the way a
    // hand written list of verbs would.
    MainWindow* w = new MainWindow;
    w->resize(1200, 800);
    w->show();
    for (int i = 0; i < 30; ++i)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);

    // The command bar and the tool strip both live in toolbars, and the tool
    // strip's actions are named "tool.<id>". Keep the two families apart: a tool
    // icon and a command icon are drawn by different code and must be compared
    // within their own family.
    QSet<QString> commandIds;
    for (QToolBar* bar : w->findChildren<QToolBar*>()) {
        for (QAction* a : bar->actions()) {
            const QString name = a->objectName();
            if (name.isEmpty() || a->icon().isNull())
                continue;
            if (name.startsWith(QLatin1String("tool.")))
                continue;
            commandIds << name;
        }
    }
    QStringList cmdIds(commandIds.begin(), commandIds.end());
    cmdIds.sort();
    w->deleteLater();
    for (int i = 0; i < 10; ++i)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);

    // What an id with no drawing produces, for both families.
    const QImage toolFallback = Icons::toolPixmap(QStringLiteral("no-such-tool"), 32).toImage();
    const QImage cmdFallback = Icons::commandPixmap(QStringLiteral("no-such-command"), 32).toImage();

    QStringList noArt;
    QList<QImage> rendered;
    QStringList names;
    for (const QString& id : std::as_const(toolIds)) {
        const QImage img = Icons::toolPixmap(id, 32).toImage();
        if (img.isNull() || !hasInk(img) || img == toolFallback)
            noArt << QStringLiteral("tool %1").arg(id);
        rendered << img;
        names << id;
    }
    for (const QString& name : std::as_const(cmdIds)) {
        // The action is "file.new" but the artwork is keyed on the verb.
        QString verb = name.mid(name.indexOf(QLatin1Char('.')) + 1);
        if (verb == QLatin1String("all"))
            verb = QStringLiteral("selectall");
        const QImage img = Icons::commandPixmap(verb, 32).toImage();
        if (img.isNull() || !hasInk(img) || img == cmdFallback)
            noArt << QStringLiteral("command %1").arg(name);
        rendered << img;
        names << name;
    }
    // The same check over every verb a menu can name, whether or not it sits on
    // the command bar.
    QStringList verbsMissing;
    for (const QString& verb : allCommandVerbs()) {
        const QImage img = Icons::commandPixmap(verb, 32).toImage();
        if (img.isNull() || !hasInk(img) || img == cmdFallback)
            verbsMissing << verb;
    }
    check(verbsMissing.isEmpty(),
          verbsMissing.isEmpty()
              ? QStringLiteral("all %1 menu verbs have a glyph")
                    .arg(allCommandVerbs().size())
              : QStringLiteral("menu verbs without a glyph: %1")
                    .arg(verbsMissing.join(QStringLiteral(", "))));

    check(noArt.isEmpty(),
          noArt.isEmpty()
              ? QStringLiteral("all %1 tools and %2 commands have their own drawing")
                    .arg(toolIds.size()).arg(cmdIds.size())
              : QStringLiteral("falling through to the placeholder: %1")
                    .arg(noArt.join(QStringLiteral(", "))));

    // Two ids that render to the same pixels are indistinguishable to the user.
    QStringList twins;
    for (int i = 0; i < rendered.size(); ++i)
        for (int j = i + 1; j < rendered.size(); ++j)
            if (rendered.at(i) == rendered.at(j))
                twins << QStringLiteral("%1 == %2").arg(names.at(i), names.at(j));
    check(twins.isEmpty(),
          twins.isEmpty() ? QStringLiteral("no two icons render the same (%1 distinct)")
                                .arg(rendered.size())
                          : QStringLiteral("identical drawings: %1")
                                .arg(twins.join(QStringLiteral(", "))));

    // ----------------------------------------------------------- contact sheet
    {
        const int cell = 86, icon = 46, cols = 8;
        const int toolRows = (toolIds.size() + cols - 1) / cols;
        const int cmdRows = (cmdIds.size() + cols - 1) / cols;
        QImage sheet(cols * cell + 16, 34 + toolRows * cell + 34 + cmdRows * cell,
                     QImage::Format_ARGB32);
        sheet.fill(QColor("#c9ced3"));
        QPainter p(&sheet);
        p.setRenderHint(QPainter::Antialiasing);
        int y = 0;
        auto section = [&](const QString& title, const QStringList& ids, bool cmd) {
            QFont bold = p.font();
            bold.setBold(true);
            bold.setPointSize(10);
            p.setFont(bold);
            p.setPen(QColor("#111"));
            p.drawText(8, y + 18, title);
            QFont plain = p.font();
            plain.setBold(false);
            plain.setPointSize(7);
            p.setFont(plain);
            for (int i = 0; i < ids.size(); ++i) {
                const int x = (i % cols) * cell + 10;
                const int cy = y + 28 + (i / cols) * cell;
                QString verb = ids.at(i);
                if (cmd) {
                    verb = verb.mid(verb.indexOf(QLatin1Char('.')) + 1);
                    if (verb == QLatin1String("all"))
                        verb = QStringLiteral("selectall");
                }
                const QPixmap pm = cmd ? Icons::commandPixmap(verb, icon)
                                       : Icons::toolPixmap(ids.at(i), icon);
                p.setPen(QColor("#d7dbe0"));
                p.drawRect(x - 3, cy - 3, icon + 6, icon + 6);
                p.setPen(Qt::NoPen);
                p.drawPixmap(x, cy, pm);
                p.setPen(QColor("#111"));
                p.drawText(x - 4, cy + icon + 14, ids.at(i));
            }
            y += 34 + ((ids.size() + cols - 1) / cols) * cell;
        };
        section(QStringLiteral("tools (%1)").arg(toolIds.size()), toolIds, false);
        section(QStringLiteral("commands (%1)").arg(cmdIds.size()), cmdIds, true);

        // The application mark at the sizes a desktop uses.
        p.setPen(QColor("#111"));
        p.drawText(8, y + 18, QStringLiteral("application mark"));
        int x = 10;
        for (const int s : { 16, 22, 24, 32, 48, 64, 128 }) {
            p.drawPixmap(QPoint(x, y + 28), Icons::app().pixmap(s, s));
            x += qMax(s, 32) + 12;
        }
        p.end();
        const QString path = QDir::tempPath() + QStringLiteral("/pnq_icons.png");
        sheet.save(path);
        std::printf("  contact sheet: %s\n", qPrintable(path));
    }

    std::printf("=== %s (%d failures) ===\n", g_failures == 0 ? "ALL PASSED" : "FAILURES",
                g_failures);
    return g_failures == 0 ? 0 : 1;
}