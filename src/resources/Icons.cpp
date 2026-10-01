#include "resources/Icons.h"

#include <QCache>
#include <QPainterPath>
#include <QPainter>
#include <QPainterPath>
#include <QPolygonF>
#include <QString>

namespace pnq {
namespace Icons {

namespace {

void drawBrushMark(QPainter& p, const QPointF& tip, qreal length, qreal angleDeg);
void drawRotateGlyph(QPainter& p, const QColor& ink);
void drawFlipGlyph(QPainter& p, const QColor& ink);
void drawZoomGlyph(QPainter& p, const QColor& ink);
void drawCropGlyph(QPainter& p, const QColor& ink);
void drawResizeGlyph(QPainter& p, const QColor& ink, const QColor& accent);

/// Line art colour, switched by the theme; see setInk().
QColor g_ink(30, 30, 30);

// The two pixmaps below are cached; the ink lives in these caches, so both are
// dropped together when the theme changes.
QCache<QString, QPixmap>* toolCache()
{
    static QCache<QString, QPixmap> cache;
    return &cache;
}
QCache<QString, QPixmap>* commandCache()
{
    static QCache<QString, QPixmap> cache;
    return &cache;
}

QPixmap makeToolPixmap(const QString& id, int size)
{
    QPixmap pm(size, size);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);
    const qreal s = size / 32.0;
    p.scale(s, s);
    // A 1.6 px hairline disappears at toolbar sizes on a light chrome, which
    // made several tools look like an empty slot. 2.3 px with a pale outline
    // behind it keeps every icon readable against both light and dark panels.
    QPen pen(QColor(30, 30, 30));
    pen.setWidthF(2.3);
    pen.setJoinStyle(Qt::RoundJoin);
    pen.setCapStyle(Qt::RoundCap);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);

    const auto stroke = [&](const QString& col = g_ink.name()) {
        p.setPen(QPen(QColor(col), 2.3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    };
    const auto fill = [&](const QString& col) { p.setBrush(QColor(col)); };

    if (id == QLatin1String("pencil")) {
        QPainterPath path;
        path.moveTo(6, 26);
        path.lineTo(8, 19);
        path.lineTo(20, 7);
        path.lineTo(25, 12);
        path.lineTo(13, 24);
        path.closeSubpath();
        p.setPen(Qt::NoPen);
        p.setBrush(QColor("#f2c14e"));
        p.drawPath(path);
        stroke();
        p.setBrush(QColor("#e8d5b0"));
        p.drawPath(path);
        p.setPen(QPen(QColor("#2a2a2a"), 1.2));
        p.drawLine(8, 19, 13, 24);
        p.setBrush(QColor("#3a3a3a"));
        p.drawEllipse(QRectF(5, 25, 4, 4));
    } else if (id == QLatin1String("brush")) {
        // The very brush the application logo carries, so the icon in the tool
        // strip and the mark in the title bar are one and the same drawing.
        drawBrushMark(p, QPointF(6.5, 25.0), 24.0, -42.0);
    } else if (id == QLatin1String("eraser")) {
        QPainterPath path;
        path.moveTo(8, 20);
        path.lineTo(18, 8);
        path.lineTo(26, 16);
        path.lineTo(16, 28);
        path.closeSubpath();
        p.setPen(Qt::NoPen);
        p.setBrush(QColor("#e88f8f"));
        p.drawPath(path);
        stroke();
        p.setBrush(QColor("#f6b9b9"));
        p.drawPath(path);
        p.setPen(QPen(QColor("#2a2a2a"), 1.2));
        p.drawLine(8, 20, 18, 28);
    } else if (id == QLatin1String("bucket") || id == QLatin1String("fill")) {
        QPainterPath path;
        path.moveTo(16, 4);
        path.lineTo(28, 16);
        path.lineTo(17, 27);
        path.lineTo(5, 15);
        path.closeSubpath();
        p.setPen(Qt::NoPen);
        p.setBrush(QColor("#8fb8e8"));
        p.drawPath(path);
        stroke();
        p.setBrush(QColor("#8fb8e8"));
        p.drawPath(path);
        p.setBrush(QColor("#2f6fb5"));
        p.drawEllipse(QRectF(24, 24, 7, 5));
    } else if (id == QLatin1String("picker")) {
        stroke();
        p.drawLine(20, 12, 27, 5);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor("#8fb8e8"));
        p.drawEllipse(QRectF(6, 18, 9, 9));
        stroke();
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(QRectF(6, 18, 9, 9));
    } else if (id == QLatin1String("gradient")) {
        QLinearGradient g(0, 0, 0, 28);
        g.setColorAt(0.0, QColor("#ffffff"));
        g.setColorAt(1.0, QColor("#202020"));
        p.fillRect(QRectF(4, 4, 24, 24), g);
        p.setPen(QPen(QColor("#2a2a2a"), 1.2));
        p.setBrush(Qt::NoBrush);
        p.drawRect(QRectF(4, 4, 24, 24));
    } else if (id == QLatin1String("text")) {
        stroke();
        p.setBrush(QColor("#2a2a2a"));
        p.setFont(QFont("Sans Serif", 20, QFont::Bold));
        p.drawText(QRectF(0, 0, 32, 30), Qt::AlignCenter, QStringLiteral("T"));
    } else if (id == QLatin1String("line")) {
        stroke();
        p.drawLine(5, 26, 26, 6);
        p.setBrush(QColor("#2a2a2a"));
        p.drawEllipse(QRectF(3, 24, 4, 4));
        p.drawEllipse(QRectF(24, 4, 4, 4));
    } else if (id == QLatin1String("rectangle") || id == QLatin1String("rect")) {
        stroke();
        p.setBrush(QColor("#9fd0a0"));
        p.drawRect(QRectF(5, 8, 22, 16));
        p.setBrush(Qt::NoBrush);
        p.drawRect(QRectF(5, 8, 22, 16));
    } else if (id == QLatin1String("ellipse") || id == QLatin1String("circle")) {
        stroke();
        p.setBrush(QColor("#9fd0a0"));
        p.drawEllipse(QRectF(4, 7, 24, 18));
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(QRectF(4, 7, 24, 18));
    } else if (id == QLatin1String("polygon") || id == QLatin1String("triangle")) {
        // A regular many sided figure, which is what clicking out points makes.
        p.setPen(QPen(QColor(g_ink), 2.3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.setBrush(QColor("#d8b48f"));
        p.drawPolygon(QPolygonF({ QPointF(16, 4), QPointF(27.6, 10.5), QPointF(23.8, 24),
                                  QPointF(8.2, 24), QPointF(4.4, 10.5) }));
        p.setBrush(Qt::NoBrush);
        p.drawPolygon(QPolygonF({ QPointF(16, 4), QPointF(27.6, 10.5), QPointF(23.8, 24),
                                  QPointF(8.2, 24), QPointF(4.4, 10.5) }));
    } else if (id == QLatin1String("freeform")) {
        // An irregular closed shape: the points were dragged, not clicked.
        p.setPen(QPen(QColor(g_ink), 2.3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.setBrush(QColor("#d8b48f"));
        p.drawPolygon(QPolygonF({ QPointF(7, 22), QPointF(10, 9), QPointF(20, 5), QPointF(27, 13),
                                  QPointF(24, 26), QPointF(13, 28) }));
        p.setBrush(Qt::NoBrush);
        p.drawPolygon(QPolygonF({ QPointF(7, 22), QPointF(10, 9), QPointF(20, 5), QPointF(27, 13),
                                  QPointF(24, 26), QPointF(13, 28) }));
    } else if (id == QLatin1String("rotate")) {
        drawRotateGlyph(p, g_ink);
    } else if (id == QLatin1String("flip")) {
        drawFlipGlyph(p, g_ink);
    } else if (id == QLatin1String("selecttransform")) {
        // A marching selection being dragged: dashed box with corner handles.
        p.setPen(QPen(QColor(g_ink), 1.8, Qt::DashLine, Qt::RoundCap));
        p.setBrush(Qt::NoBrush);
        p.drawRect(QRectF(7, 7, 18, 18));
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(g_ink));
        for (const QPointF& c : { QPointF(7, 7), QPointF(25, 7), QPointF(7, 25), QPointF(25, 25) })
            p.drawRect(QRectF(c.x() - 2.2, c.y() - 2.2, 4.4, 4.4));
        p.setPen(QPen(QColor(g_ink), 2.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.setBrush(Qt::NoBrush);
        p.drawLine(QPointF(11, 21), QPointF(21, 11));
        p.drawPolygon(QPolygonF({ QPointF(11, 21), QPointF(11, 15.5), QPointF(15.5, 21) }));
        p.drawPolygon(QPolygonF({ QPointF(21, 11), QPointF(15.5, 11), QPointF(21, 15.5) }));
    } else if (id == QLatin1String("transform")) {
        // The whole layer being scaled: a box with handles and a double headed
        // arrow along the diagonal, which reads apart from the selection tool.
        p.setPen(QPen(QColor(g_ink), 2.3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.setBrush(QColor("#8fb8e8"));
        p.drawRect(QRectF(5, 8, 22, 16));
        p.setPen(QPen(QColor("#ffffff"), 2.0, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(QPointF(10, 20), QPointF(22, 12));
        p.setPen(QPen(QColor("#ffffff"), 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.drawPolygon(QPolygonF({ QPointF(10, 20), QPointF(10, 15.6), QPointF(14.4, 20) }));
        p.drawPolygon(QPolygonF({ QPointF(22, 12), QPointF(17.6, 12), QPointF(22, 16.4) }));
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(g_ink));
        for (const QPointF& c : { QPointF(5, 8), QPointF(27, 8), QPointF(5, 24), QPointF(27, 24) })
            p.drawRect(QRectF(c.x() - 2.0, c.y() - 2.0, 4.0, 4.0));
    } else if (id == QLatin1String("curve")) {
        stroke();
        QPainterPath path;
        path.moveTo(4, 24);
        path.cubicTo(12, 4, 20, 28, 28, 8);
        p.drawPath(path);
    } else if (id == QLatin1String("selectrect")) {
        stroke("#2a2a2a");
        p.setBrush(QColor(0, 0, 0, 0));
        p.setBrush(Qt::NoBrush);
        for (int i = 0; i < 4; ++i) {
            p.drawRect(QRectF(5 + i * 6, 6, 16, 18));
        }
    } else if (id == QLatin1String("selecttransparent")) {
        // An opaque area selected over a checkerboard: the transparent pixels are
        // picked out by their background, which is what the tool looks for.
        p.setPen(QPen(QColor(g_ink), 2.3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.setBrush(QColor("#ffffff"));
        p.drawRoundedRect(QRectF(4, 6, 24, 20), 2, 2);
        const qreal cell = 4.0;
        p.setPen(Qt::NoPen);
        p.setBrush(QColor("#c9ccd1"));
        for (int row = 0; row < 5; ++row)
            for (int col = 0; col < 6; ++col)
                if (((row + col) % 2) == 0)
                    p.drawRect(QRectF(5 + col * cell, 7 + row * cell, cell, cell));
        p.setPen(QPen(QColor(g_ink), 2.3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.setBrush(QColor("#9fd0a0"));
        p.drawRoundedRect(QRectF(11, 11, 12, 9), 1.5, 1.5);
    } else if (id == QLatin1String("selectellipse")) {
        stroke();
        for (int i = 0; i < 3; ++i)
            p.drawEllipse(QRectF(4 + i * 3, 5 + i * 2, 24 - i * 6, 20 - i * 4));
    } else if (id == QLatin1String("lasso") || id == QLatin1String("freeformselect")) {
        stroke();
        QPainterPath path;
        path.moveTo(8, 20);
        path.cubicTo(2, 10, 18, 2, 24, 10);
        path.cubicTo(28, 18, 18, 26, 10, 24);
        p.drawPath(path);
        p.drawLine(8, 20, 5, 29);
    } else if (id == QLatin1String("wand")) {
        stroke();
        p.drawLine(9, 23, 23, 9);
        p.setPen(QPen(QColor("#2a2a2a"), 1.6));
        p.setBrush(QColor("#ffd76e"));
        p.drawPolygon(QPolygonF({ QPointF(22, 3), QPointF(26, 10), QPointF(18, 10) }));
        for (int i = 0; i < 4; ++i) {
            p.drawLine(4 + i * 3, 3 + (i % 2) * 3, 4 + i * 3, 7 + (i % 2) * 3);
        }
    } else if (id == QLatin1String("move")) {
        stroke();
        p.drawLine(16, 4, 16, 28);
        p.drawLine(4, 16, 28, 16);
        p.setBrush(QColor("#2a2a2a"));
        QPolygonF tri;
        tri << QPointF(16, 2) << QPointF(12, 8) << QPointF(20, 8);
        p.drawPolygon(tri);
        tri.clear();
        tri << QPointF(16, 30) << QPointF(12, 24) << QPointF(20, 24);
        p.drawPolygon(tri);
        tri.clear();
        tri << QPointF(2, 16) << QPointF(8, 12) << QPointF(8, 20);
        p.drawPolygon(tri);
        tri.clear();
        tri << QPointF(30, 16) << QPointF(24, 12) << QPointF(24, 20);
        p.drawPolygon(tri);
    } else if (id == QLatin1String("zoom")) {
        stroke();
        p.drawEllipse(QRectF(5, 5, 16, 16));
        p.drawLine(19, 19, 27, 27);
        p.setPen(QPen(QColor("#2a2a2a"), 1.2));
        p.drawLine(9, 13, 17, 13);
        p.drawLine(13, 9, 13, 17);
    } else if (id == QLatin1String("pan")) {
        stroke();
        p.drawEllipse(QRectF(9, 4, 14, 12));
        p.drawLine(6, 27, 9, 17);
        p.drawLine(6, 27, 14, 27);
        p.drawLine(26, 27, 23, 17);
        p.drawLine(26, 27, 18, 27);
        p.drawLine(16, 17, 16, 24);
    } else if (id == QLatin1String("blur")) {
        stroke();
        p.drawEllipse(QRectF(6, 8, 14, 14));
        p.setPen(QPen(QColor("#2a2a2a"), 1.0, Qt::DashLine));
        p.drawEllipse(QRectF(3, 5, 20, 20));
        p.drawEllipse(QRectF(9, 11, 20, 20));
    } else if (id == QLatin1String("smudge")) {
        stroke();
        p.drawLine(6, 26, 14, 18);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor("#c9a06a"));
        p.drawEllipse(QRectF(13, 8, 12, 10));
    } else if (id == QLatin1String("dodgeburn")) {
        QLinearGradient g(0, 0, 32, 0);
        g.setColorAt(0, QColor("#333"));
        g.setColorAt(0.5, QColor("#fff"));
        g.setColorAt(1, QColor("#333"));
        p.fillRect(QRectF(4, 10, 24, 12), g);
        p.setPen(QPen(QColor("#2a2a2a"), 1.2));
        p.setBrush(Qt::NoBrush);
        p.drawRect(QRectF(4, 10, 24, 12));
    } else if (id == QLatin1String("recolor") || id == QLatin1String("replacecolor")) {
        stroke();
        p.setBrush(QColor("#e0e0e0"));
        p.drawEllipse(QRectF(4, 4, 12, 12));
        p.setBrush(QColor("#c04040"));
        p.drawEllipse(QRectF(16, 16, 12, 12));
        p.setPen(QPen(QColor("#2a2a2a"), 1.2, Qt::SolidLine));
        p.drawLine(8, 20, 24, 12);
    } else if (id == QLatin1String("shapes") || id == QLatin1String("newimage")
               || id == QLatin1String("open") || id == QLatin1String("save")) {
        stroke();
        p.drawRect(QRectF(4, 4, 24, 24));
        p.setBrush(QColor("#8fb8e8"));
        p.drawEllipse(QRectF(8, 8, 10, 10));
        p.setBrush(QColor("#d8b48f"));
        p.drawPolygon(QPolygonF({ QPointF(6, 26), QPointF(16, 14), QPointF(28, 26) }));
    } else {
        p.setPen(QPen(QColor("#2a2a2a"), 1.6));
        p.setBrush(QColor("#8fb8e8"));
        p.drawRoundedRect(QRectF(5, 5, 22, 22), 4, 4);
    }
    p.end();
    return pm;
}


/// A circular arrow. Used by the rotate tool and by the rotate command, so the
/// two are literally the same drawing.
void drawRotateGlyph(QPainter& p, const QColor& ink)
{
    QPen pen(ink, 2.3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    QPainterPath arc;
    arc.moveTo(25.0, 10.5);
    arc.arcTo(QRectF(6.5, 6.5, 19.0, 19.0), 300, -250);
    p.drawPath(arc);
    p.setPen(Qt::NoPen);
    p.setBrush(ink);
    p.drawPolygon(QPolygonF({ QPointF(20.6, 4.2), QPointF(28.2, 6.4), QPointF(25.4, 13.6) }));
}

/// Two halves meeting on a dashed line: a mirror.
void drawFlipGlyph(QPainter& p, const QColor& ink)
{
    p.setPen(QPen(ink, 2.3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(Qt::NoBrush);
    QPainterPath left;
    left.moveTo(15.0, 5.0);
    left.lineTo(5.0, 16.0);
    left.lineTo(15.0, 27.0);
    left.closeSubpath();
    p.drawPath(left);
    p.setBrush(QColor("#9fd0a0"));
    p.drawPath(left);
    QPainterPath right;
    right.moveTo(17.0, 5.0);
    right.lineTo(27.0, 16.0);
    right.lineTo(17.0, 27.0);
    right.closeSubpath();
    p.setBrush(Qt::NoBrush);
    p.drawPath(right);
    p.setPen(QPen(ink, 1.5, Qt::DashLine, Qt::RoundCap));
    p.drawLine(QPointF(16.0, 4.0), QPointF(16.0, 28.0));
}

/// The magnifier, shared by the zoom tool and the zoom command.
void drawZoomGlyph(QPainter& p, const QColor& ink)
{
    p.setPen(QPen(ink, 2.3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(Qt::NoBrush);
    p.drawEllipse(QRectF(6.0, 6.0, 15.0, 15.0));
    p.drawLine(QPointF(19.0, 19.0), QPointF(26.5, 26.5));
    p.setPen(QPen(ink, 1.5, Qt::SolidLine, Qt::RoundCap));
    p.drawLine(QPointF(10.0, 13.5), QPointF(17.0, 13.5));
    p.drawLine(QPointF(13.5, 10.0), QPointF(13.5, 17.0));
}


/// Resize: one frame being stretched into a larger one.
void drawResizeGlyph(QPainter& p, const QColor& ink, const QColor& accent)
{
    p.setPen(QPen(ink, 2.3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(QColor("#dfe7f0"));
    p.drawRect(QRectF(4, 9, 17, 14));
    p.setBrush(Qt::NoBrush);
    p.drawRect(QRectF(4, 9, 17, 14));
    p.setPen(QPen(accent, 2.3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(Qt::NoBrush);
    p.drawRect(QRectF(11, 6, 17, 20));
}

/// Crop marks: two overlapping corners that trim a picture down.
void drawCropGlyph(QPainter& p, const QColor& ink)
{
    p.setPen(QPen(ink, 2.3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.drawPolyline(QPolygonF({ QPointF(4.0, 12.0), QPointF(4.0, 4.0), QPointF(12.0, 4.0) }));
    p.drawPolyline(QPolygonF({ QPointF(20.0, 4.0), QPointF(28.0, 4.0), QPointF(28.0, 12.0) }));
    p.drawPolyline(QPolygonF({ QPointF(28.0, 20.0), QPointF(28.0, 28.0), QPointF(20.0, 28.0) }));
    p.drawPolyline(QPolygonF({ QPointF(12.0, 28.0), QPointF(4.0, 28.0), QPointF(4.0, 20.0) }));
    p.setBrush(QColor("#2f7fd4"));
    p.setPen(Qt::NoPen);
    p.drawRect(QRectF(12.0, 12.0, 8.0, 8.0));
}

/// The application's own brush, drawn from `tip` along `angleDeg`, bristles at
/// the near end and handle running away from it.
///
/// Shared by the brush tool icon and the application logo, so the brush on the
/// logo is literally the brush the program draws with rather than a lookalike.
void drawBrushMark(QPainter& p, const QPointF& tip, qreal length, qreal angleDeg)
{
    p.save();
    p.translate(tip);
    p.rotate(angleDeg);
    const qreal L = length;
    const qreal rB = L * 0.042; // half height of the ferrule
    const qreal rH = L * 0.032; // half height of the handle
    p.setPen(Qt::NoPen);

    // Handle: a slightly tapered wooden shaft with a rounded far end.
    QPainterPath handle;
    handle.moveTo(L * 0.33, -rH);
    handle.lineTo(L * 0.96, -rH * 0.80);
    handle.quadTo(L * 1.0, 0.0, L * 0.96, rH * 0.80);
    handle.lineTo(L * 0.33, rH);
    handle.closeSubpath();
    QLinearGradient wood(L * 0.33, 0, L, 0);
    wood.setColorAt(0.00, QColor("#c58a4d"));
    wood.setColorAt(0.40, QColor("#8d5526"));
    wood.setColorAt(1.00, QColor("#5b3415"));
    p.setBrush(wood);
    p.drawPath(handle);

    // Ferrule: the metal band that holds the bristles.
    QLinearGradient metal(L * 0.23, 0, L * 0.34, 0);
    metal.setColorAt(0.0, QColor("#eef1f4"));
    metal.setColorAt(0.45, QColor("#a9b1b8"));
    metal.setColorAt(1.0, QColor("#dfe4e8"));
    p.setBrush(metal);
    p.drawRoundedRect(QRectF(L * 0.23, -rB * 1.04, L * 0.11, rB * 2.08), rB * 0.3, rB * 0.3);

    // Bristles, dark at the ferrule and loaded with paint at the tip.
    QPainterPath bristles;
    bristles.moveTo(0.0, 0.0);
    bristles.lineTo(L * 0.235, -rB);
    bristles.lineTo(L * 0.235, rB);
    bristles.closeSubpath();
    QLinearGradient paint(0, 0, L * 0.235, 0);
    paint.setColorAt(0.0, QColor("#ef4f66"));
    paint.setColorAt(0.45, QColor("#d02b47"));
    paint.setColorAt(1.0, QColor("#7d1526"));
    p.setBrush(paint);
    p.drawPath(bristles);
    p.restore();
}

/// The window shown inside the logo: a KDE style window with a title bar, a
/// toolbar and a canvas carrying a stroke of paint. Below 40 px the toolbar and
/// the window buttons are dropped, because at that size they only turn to mush.
void drawLogoWindow(QPainter& p, const QRectF& win, bool detailed)
{
    const qreal titleH = win.height() * (detailed ? 0.19 : 0.22);
    const qreal toolH = detailed ? win.height() * 0.115 : 0.0;

    // Title bar, the pale gradient Breeze uses.
    QLinearGradient title(QPointF(0, win.top()), QPointF(0, win.top() + titleH));
    title.setColorAt(0.0, QColor("#fdfdfe"));
    title.setColorAt(1.0, QColor("#e6e9ed"));
    p.setPen(Qt::NoPen);
    p.setBrush(title);
    p.drawRect(QRectF(win.left(), win.top(), win.width(), titleH));

    if (detailed) {
        // Window buttons: minimise, maximise, close.
        const qreal r = win.height() * 0.030;
        const qreal by = win.top() + titleH * 0.5;
        const qreal bx = win.right() - r * 1.9;
        p.setBrush(QColor("#fdbc4b"));
        p.drawEllipse(QPointF(bx - r * 3.0, by), r, r);
        p.setBrush(QColor("#3daee9"));
        p.drawEllipse(QPointF(bx - r * 1.5, by), r, r);
        p.setBrush(QColor("#da4453"));
        p.drawEllipse(QPointF(bx, by), r, r);
        // Title text as two grey bars; real text is unreadable at icon sizes.
        p.setBrush(QColor("#9aa0a8"));
        p.drawRoundedRect(QRectF(win.left() + win.width() * 0.10, by - titleH * 0.16,
                                 win.width() * 0.34, titleH * 0.30),
                          titleH * 0.15, titleH * 0.15);
        p.setBrush(QColor("#c2c7cd"));
        p.drawRoundedRect(QRectF(win.left() + win.width() * 0.10, by + titleH * 0.22,
                                 win.width() * 0.20, titleH * 0.22),
                          titleH * 0.11, titleH * 0.11);
    }

    // Toolbar strip with a row of small buttons.
    if (toolH > 0.0) {
        const QRectF bar(win.left(), win.top() + titleH, win.width(), toolH);
        p.setBrush(QColor("#f2f4f7"));
        p.drawRect(bar);
        p.setPen(QPen(QColor("#d3d8de"), 0.35));
        p.drawLine(bar.left(), bar.bottom(), bar.right(), bar.bottom());
        const qreal bh = bar.height() * 0.46;
        const qreal by2 = bar.center().y();
        const qreal gap = bar.width() * 0.115;
        qreal bx2 = bar.left() + bar.width() * 0.07;
        for (int i = 0; i < 5; ++i) {
            p.setPen(Qt::NoPen);
            p.setBrush(i == 1 ? QColor("#2f7fd4") : QColor("#8b9199"));
            p.drawRoundedRect(QRectF(bx2, by2 - bh * 0.5, bh, bh), bh * 0.28, bh * 0.28);
            bx2 += gap;
        }
    }

    // Canvas. Below 40 px a plain white rectangle reads as an empty box, so the
    // small sizes get a tinted canvas to keep some colour in the mark.
    const QRectF canvas(win.left(), win.top() + titleH + toolH, win.width(),
                        win.height() - titleH - toolH);
    p.setBrush(detailed ? QColor("#ffffff") : QColor("#e8f1fb"));
    p.drawRect(canvas);
    p.setPen(QPen(QColor("#c9cfd6"), 0.4));
    p.setBrush(Qt::NoBrush);
    p.drawRect(canvas);

    // A short swash of paint in the top left of the canvas. It is deliberately
    // short: the brush crosses the window on the diagonal, and a stroke that ran
    // the full width would pile up with it into one unreadable smear.
    const qreal top = canvas.top() + canvas.height() * 0.25;
    const qreal x0 = canvas.left() + canvas.width() * 0.09;
    const qreal x1 = canvas.left() + canvas.width() * 0.46;
    QPainterPath swash;
    swash.moveTo(x0, top);
    swash.cubicTo(x0 + (x1 - x0) * 0.30, top - canvas.height() * 0.13,
                  x0 + (x1 - x0) * 0.62, top + canvas.height() * 0.08, x1, top - canvas.height() * 0.06);
    // The gradient follows the stroke, not the whole canvas, so the short
    // swash still shows all three colours instead of staying flat blue.
    QLinearGradient ink(x0, top, x1, top);
    ink.setColorAt(0.0, QColor("#2f7fd4"));
    ink.setColorAt(0.55, QColor("#7b5ad6"));
    ink.setColorAt(1.0, QColor("#e0407f"));
    p.setPen(QPen(ink, canvas.height() * (detailed ? 0.11 : 0.17), Qt::SolidLine, Qt::RoundCap,
                  Qt::RoundJoin));
    p.drawPath(swash);
}

/// The application mark: a photo frame holding a KDE window, with the program's
/// own brush laid across it. That is the composition of the Paint.NET logo, with
/// the photograph replaced by a window this program would really show.
QPixmap makeAppPixmap(int size)
{
    const bool detailed = size >= 40;
    QPixmap pm(size, size);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.scale(size / 100.0, size / 100.0);

    const qreal inset = size >= 24 ? 7.0 : 5.0;
    const QRectF frame(inset, inset, 100.0 - inset * 2, 100.0 - inset * 2);
    const qreal radius = detailed ? 7.0 : 5.0;

    // Shadow under the frame.
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0, 0, 0, 42));
    p.drawRoundedRect(frame.adjusted(0.5, 2.4, 0.5, 3.2), radius, radius);

    // The frame itself.
    p.setPen(QPen(QColor(detailed ? "#a9afb6" : "#7e8894"), detailed ? 1.0 : 1.6));
    p.setBrush(QColor(detailed ? "#fcfcfd" : "#dde5ee"));
    p.drawRoundedRect(frame, radius, radius);

    // Window inside the frame.
    const qreal pad = detailed ? 7.5 : 5.5;
    drawLogoWindow(p, frame.adjusted(pad, pad, -pad, -pad), detailed);

    // Brush across the frame, bristles at the lower left like the original.
    // The shadow is the brush drawn a few times with growing offsets and faint
    // ink; a single offset copy reads as a hard dark edge rather than a shadow.
    const QPointF tip(15.0, 82.0);
    for (int i = 3; i >= 1; --i) {
        p.save();
        p.translate(i * 0.75, i * 0.95);
        p.setOpacity(0.16);
        drawBrushMark(p, tip, 86.0, -45.0);
        p.restore();
    }
    p.setOpacity(1.0);
    drawBrushMark(p, tip, 86.0, -45.0);

    p.end();
    return pm;
}

/// Icons for the command bar, drawn in the same flat dark-line style as the tool
/// icons so the bar reads as icons rather than as a row of words.
QPixmap makeCommandPixmap(const QString& id, int size)
{
    QPixmap pm(size, size);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.scale(size / 32.0, size / 32.0);

    const QString ink = g_ink.name();
    const QString paper = QStringLiteral("#f7f7f7");
    const QString accent = QStringLiteral("#2f7fd4");
    auto pen = [&](const QString& col, qreal w = 1.8, Qt::PenStyle style = Qt::SolidLine) {
        p.setPen(QPen(QColor(col), w, style, Qt::RoundCap, Qt::RoundJoin));
    };
    auto sheet = [&]() {
        p.setPen(QPen(QColor(ink), 2.3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.setBrush(QColor(paper));
        QPainterPath path;
        path.moveTo(6, 4);
        path.lineTo(20, 4);
        path.lineTo(26, 10);
        path.lineTo(26, 28);
        path.lineTo(6, 28);
        path.closeSubpath();
        p.drawPath(path);
        p.setPen(QPen(QColor(ink), 1.4));
        p.setBrush(Qt::NoBrush);
        p.drawLine(QPointF(20, 4), QPointF(20, 10));
        p.drawLine(QPointF(20, 10), QPointF(26, 10));
        pen(ink, 1.3);
        for (int i = 0; i < 3; ++i)
            p.drawLine(QPointF(10, 16 + i * 4), QPointF(21, 16 + i * 4));
    };

    if (id == QLatin1String("new")) {
        sheet();
        p.setPen(QPen(QColor(accent), 2.4, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(QPointF(19, 19), QPointF(28, 19));
        p.drawLine(QPointF(23.5, 14.5), QPointF(23.5, 23.5));
    } else if (id == QLatin1String("open")) {
        p.setPen(QPen(QColor(ink), 2.3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.setBrush(QColor(QStringLiteral("#e0b45c")));
        QPainterPath back;
        back.moveTo(4, 26);
        back.lineTo(4, 9);
        back.lineTo(13, 9);
        back.lineTo(16, 12);
        back.lineTo(28, 12);
        back.lineTo(28, 26);
        back.closeSubpath();
        p.drawPath(back);
        p.setBrush(QColor(QStringLiteral("#ffdf9a")));
        QPainterPath front;
        front.moveTo(4, 26);
        front.lineTo(9.5, 15);
        front.lineTo(30, 15);
        front.lineTo(24.5, 26);
        front.closeSubpath();
        p.drawPath(front);
    } else if (id == QLatin1String("save")) {
        p.setPen(QPen(QColor(ink), 2.3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.setBrush(QColor(QStringLiteral("#3a6fb0")));
        p.drawRoundedRect(QRectF(5, 5, 22, 22), 2, 2);
        p.setBrush(QColor(paper));
        p.drawRect(QRectF(11, 5, 10, 8));
        p.setBrush(QColor(QStringLiteral("#c9d6e6")));
        p.drawRoundedRect(QRectF(9, 17, 14, 10), 1, 1);
    } else if (id == QLatin1String("print")) {
        p.setPen(QPen(QColor(ink), 2.3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.setBrush(QColor(QStringLiteral("#b9bcc2")));
        p.drawRoundedRect(QRectF(4, 12, 24, 10), 2, 2);
        p.setBrush(QColor(paper));
        p.drawRect(QRectF(9, 4, 14, 8));
        p.drawRect(QRectF(9, 20, 14, 8));
        pen(QStringLiteral("#5a5f66"), 1.3);
        p.drawLine(QPointF(12, 26), QPointF(20, 26));
    } else if (id == QLatin1String("cut")) {
        pen(ink, 1.8);
        p.setBrush(Qt::NoBrush);
        p.drawLine(QPointF(9, 5), QPointF(21, 21));
        p.drawLine(QPointF(23, 5), QPointF(11, 21));
        p.setPen(QPen(QColor(ink), 1.8));
        p.setBrush(QColor(QStringLiteral("#dcdcdc")));
        p.drawEllipse(QPointF(9, 25), 3.4, 3.4);
        p.drawEllipse(QPointF(23, 25), 3.4, 3.4);
    } else if (id == QLatin1String("copy")) {
        p.setPen(QPen(QColor(ink), 2.3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.setBrush(QColor(QStringLiteral("#c2c2c2")));
        p.drawRoundedRect(QRectF(4, 4, 15, 19), 1.5, 1.5);
        p.setBrush(QColor(paper));
        p.drawRoundedRect(QRectF(12, 9, 15, 19), 1.5, 1.5);
        pen(ink, 1.3);
        p.setBrush(Qt::NoBrush);
        p.drawLine(QPointF(15, 15), QPointF(24, 15));
        p.drawLine(QPointF(15, 19), QPointF(24, 19));
        p.drawLine(QPointF(15, 23), QPointF(21, 23));
    } else if (id == QLatin1String("paste")) {
        p.setPen(QPen(QColor(ink), 2.3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.setBrush(QColor(QStringLiteral("#c9b98a")));
        p.drawRoundedRect(QRectF(5, 6, 22, 22), 2, 2);
        p.setBrush(QColor(paper));
        p.drawRoundedRect(QRectF(10, 11, 15, 17), 1, 1);
        p.setBrush(QColor(QStringLiteral("#9a8a5b")));
        p.drawRect(QRectF(12, 4, 8, 5));
    } else if (id == QLatin1String("undo") || id == QLatin1String("redo")) {
        const bool left = (id == QLatin1String("undo"));
        p.setPen(QPen(QColor(ink), 2.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.setBrush(Qt::NoBrush);
        // Half circle, open at the bottom, with an arrow head on the open end.
        QPainterPath arc;
        if (left) {
            arc.moveTo(16, 9);
            arc.arcTo(QRectF(6, 9, 20, 14), 180, -180);
        } else {
            arc.moveTo(16, 9);
            arc.arcTo(QRectF(6, 9, 20, 14), 180, 180);
        }
        p.drawPath(arc);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(ink));
        if (left)
            p.drawPolygon(QPolygonF({ QPointF(2.5, 11), QPointF(8.5, 8), QPointF(8.5, 14) }));
        else
            p.drawPolygon(QPolygonF({ QPointF(29.5, 11), QPointF(23.5, 8), QPointF(23.5, 14) }));
    } else if (id == QLatin1String("selectall")) {
        pen(ink, 1.8, Qt::DashLine);
        p.setBrush(Qt::NoBrush);
        p.drawRect(QRectF(6, 6, 20, 20));
    } else if (id == QLatin1String("crop")) {
        drawCropGlyph(p, QColor(ink));
    } else if (id == QLatin1String("trim")) {
        // Trim: the sheet cut down to its content, with the offcuts left behind.
        drawCropGlyph(p, QColor(ink));
        p.setBrush(QColor(accent));
        p.setPen(Qt::NoPen);
        p.drawRect(QRectF(13.0, 13.0, 6.0, 6.0));
    } else if (id == QLatin1String("rotate90cw")) {
        p.setPen(QPen(QColor(ink), 2.3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.setBrush(Qt::NoBrush);
        p.drawRect(QRectF(9, 9, 14, 14));
        drawRotateGlyph(p, QColor(accent));
    } else if (id == QLatin1String("rotate90ccw")) {
        p.setPen(QPen(QColor(ink), 2.3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.setBrush(Qt::NoBrush);
        p.drawRect(QRectF(9, 9, 14, 14));
        p.save();
        p.translate(16.0, 16.0);
        p.rotate(180.0);
        p.translate(-16.0, -16.0);
        drawRotateGlyph(p, QColor(accent));
        p.restore();
    } else if (id == QLatin1String("flipH")) {
        drawFlipGlyph(p, QColor(ink));
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(accent));
        p.drawRect(QRectF(13.6, 12.0, 4.8, 8.0));
    } else if (id == QLatin1String("flipV")) {
        p.save();
        p.translate(16.0, 16.0);
        p.rotate(90.0);
        p.translate(-16.0, -16.0);
        drawFlipGlyph(p, QColor(ink));
        p.restore();
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(accent));
        p.drawRect(QRectF(12.0, 13.6, 8.0, 4.8));
    } else if (id == QLatin1String("resize")) {
        drawResizeGlyph(p, QColor(ink), QColor(accent));
    } else if (id == QLatin1String("selectall")) {
        p.setPen(QPen(QColor(ink), 1.8, Qt::DashLine, Qt::RoundCap));
        p.setBrush(Qt::NoBrush);
        p.drawRect(QRectF(6, 6, 20, 20));
    } else if (id == QLatin1String("rotate")) {
        drawRotateGlyph(p, QColor(ink));
    } else if (id == QLatin1String("flip")) {
        drawFlipGlyph(p, QColor(ink));
    } else if (id == QLatin1String("zoom")) {
        drawZoomGlyph(p, QColor(ink));
    } else if (id == QLatin1String("saveas")) {
        sheet();
        // The sheet with a second, smaller sheet sliding out of it.
        p.setPen(QPen(QColor(ink), 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.setBrush(QColor(paper));
        p.drawRoundedRect(QRectF(15, 15, 13, 15), 1.5, 1.5);
        pen(ink, 1.2);
        p.setBrush(Qt::NoBrush);
        p.drawLine(QPointF(18, 20), QPointF(25, 20));
        p.drawLine(QPointF(18, 24), QPointF(25, 24));
    } else if (id == QLatin1String("clear")) {
        // An empty sheet with a strike through it: clear the image away.
        p.setPen(QPen(QColor(ink), 2.3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.setBrush(QColor(paper));
        p.drawRoundedRect(QRectF(7, 5, 18, 22), 2, 2);
        p.setPen(QPen(QColor("#c0392b"), 2.6, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(QPointF(11, 9), QPointF(21, 23));
    } else if (id == QLatin1String("about")) {
        p.setPen(QPen(QColor(ink), 2.3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.setBrush(QColor(paper));
        p.drawEllipse(QRectF(4, 4, 24, 24));
        p.setPen(QPen(QColor(accent), 3.0, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(QPointF(16, 13), QPointF(16, 21));
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(accent));
        p.drawEllipse(QPointF(16, 8.6), 1.8, 1.8);
    } else if (id == QLatin1String("help")) {
        p.setPen(QPen(QColor(ink), 2.3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.setBrush(QColor(paper));
        p.drawEllipse(QRectF(4, 4, 24, 24));
        p.setPen(QPen(QColor(accent), 3.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        QPainterPath hook;
        hook.moveTo(12.4, 12.2);
        hook.cubicTo(12.4, 7.4, 20.2, 7.4, 20.2, 11.6);
        hook.cubicTo(20.2, 14.4, 16.2, 14.6, 16.2, 18.2);
        p.drawPath(hook);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(accent));
        p.drawEllipse(QPointF(16.2, 23.0), 1.8, 1.8);
    } else if (id == QLatin1String("exit")) {
        // A door with an arrow leaving it.
        p.setPen(QPen(QColor(ink), 2.3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.setBrush(QColor(paper));
        p.drawRoundedRect(QRectF(4, 6, 15, 20), 1.5, 1.5);
        p.setBrush(QColor("#9aa0a8"));
        p.drawEllipse(QPointF(15.0, 16.0), 1.3, 1.3);
        pen(accent, 2.4);
        p.drawLine(QPointF(12, 16), QPointF(28, 16));
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(accent));
        p.drawPolygon(QPolygonF({ QPointF(30, 16), QPointF(24, 12.4), QPointF(24, 19.6) }));
    } else {
        // Unknown command: a neutral dot, so the bar never shows an empty slot.
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(QStringLiteral("#b0b0b0")));
        p.drawEllipse(QPointF(16, 16), 5, 5);
    }
    return pm;
}

} // namespace

QPixmap pixmap(const QString& id, int size)
{
    QCache<QString, QPixmap>& cache = *toolCache();
    const QString key = g_ink.name() + QLatin1Char('/') + id + QStringLiteral("@%1").arg(size);
    if (QPixmap* found = cache.object(key))
        return *found;
    QPixmap pm = makeToolPixmap(id, size);
    cache.insert(key, new QPixmap(pm));
    return pm;
}

QPixmap toolPixmap(const QString& id, int size)
{
    return pixmap(id, size);
}

void setInk(const QColor& colour)
{
    g_ink = colour;
}

void clearCache()
{
    toolCache()->clear();
    commandCache()->clear();
}

QIcon tool(const QString& id)
{
    return QIcon(pixmap(id, 32));
}

QPixmap commandPixmap(const QString& id, int size)
{
    // Kept in a separate cache from the tool pixmaps so the two never collide
    // on the same key.
    QCache<QString, QPixmap>& cache = *commandCache();
    cache.setMaxCost(48 << 20);
    const QString key = QStringLiteral("cmd:") + g_ink.name() + QLatin1Char(':')
                 + id + QLatin1Char(':') + QString::number(size);
    if (QPixmap* found = cache.object(key))
        return *found;
    QPixmap pm = makeCommandPixmap(id, size);
    cache.insert(key, new QPixmap(pm));
    return pm;
}

QIcon command(const QString& id)
{
    return QIcon(commandPixmap(id, 32));
}

QIcon action(const QString& id)
{
    return command(id);
}

QIcon app()
{
    // One pixmap per size. Handing a desktop a single 128 px bitmap and letting
    // it scale is what made the window icon and the taskbar entry look soft.
    static const QIcon icon = [] {
        QIcon built;
        for (const int s : { 16, 20, 22, 24, 32, 40, 48, 64, 96, 128, 192, 256 })
            built.addPixmap(makeAppPixmap(s));
        return built;
    }();
    return icon;
}

} // namespace Icons
} // namespace pnq
