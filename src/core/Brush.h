#pragma once

#include "core/ColorUtils.h"
#include "core/Selection.h"
#include "core/Surface.h"

#include <QImage>
#include <QObject>
#include <QString>
#include <QStringList>

namespace pnq {

class Document;

enum class BrushShape {
    Round = 0,
    Square,
    ForwardSlash,
    BackSlash,
    Horizontal,
    Vertical,
    Count
};

QString brushShapeName(BrushShape s);
BrushShape brushShapeFromName(const QString& name);
QPixmap brushShapeIcon(BrushShape s, int size);

enum class BrushMode {
    Normal = 0,
    Erase
};

/// A parametric brush: size, hardness, opacity, spacing, shape, texture.
class Brush
{
public:
    Brush();
    explicit Brush(int size);

    int size() const { return m_size; }
    void setSize(int s) { m_size = qBound(1, s, 1000); }

    int hardness() const { return m_hardness; }
    void setHardness(int h) { m_hardness = qBound(0, h, 100); }

    int opacity() const { return m_opacity; }
    void setOpacity(int o) { m_opacity = qBound(0, o, 100); }

    int spacing() const { return m_spacing; }
    void setSpacing(int s) { m_spacing = qBound(1, s, 100); }

    bool antiAliasing() const { return m_antiAlias; }
    void setAntiAliasing(bool a) { m_antiAlias = a; }

    BrushShape shape() const { return m_shape; }
    void setShape(BrushShape s) { m_shape = s; }

    BrushMode mode() const { return m_mode; }
    void setMode(BrushMode m) { m_mode = m; }

    const QImage& texture() const { return m_texture; }
    void setTexture(const QImage& t) { m_texture = t; }

    QString name() const { return m_name; }
    void setName(const QString& n) { m_name = n; }
    bool isCustom() const { return m_custom; }
    void setCustom(bool c) { m_custom = c; }

    /// Distance between stamps for the given size.
    double spacingDistance() const;

    /// Grey-scale coverage stamp (0..255) for the current shape/size/hardness.
    const QImage& stamp() const;

    /// Draws a segment on the surface honouring the selection mask.
    void drawSegment(Surface& target, const QPointF& a, const QPointF& b, pixel_t color,
                     const Selection* selection, double extraOpacity = 1.0) const;
    /// Draws a single stamp.
    void drawPoint(Surface& target, const QPointF& p, pixel_t color, const Selection* selection,
                   double extraOpacity = 1.0) const;

    /// Stamps the brush over a document layer within the given rect (used for
    /// the brush cursor preview).
    Surface renderPreview(int size) const;

    /// Applies the brush to a whole area (clouds, textures).
    void fillArea(Surface& target, const QRect& area, pixel_t color, const Selection* selection,
                  double extraOpacity = 1.0) const;

    static QImage makeStamp(int size, int hardness, BrushShape shape, bool antiAlias, bool forceFull = false);
    static void setCacheLimit(int mb);

    // ----- presets -----
    static QList<Brush> defaultBrushes();
    static QStringList defaultTextureNames();

    // ----- textures (built in) -----
    static QImage generateTexture(const QString& name, int size = 64, quint32 seed = 12345);
    static QImage textureFromResource(const QString& name);

private:
    int m_size = 8;
    int m_hardness = 60;
    int m_opacity = 100;
    int m_spacing = 20;
    bool m_antiAlias = true;
    BrushShape m_shape = BrushShape::Round;
    BrushMode m_mode = BrushMode::Normal;
    QImage m_texture;
    QString m_name = QObject::tr("Brush 1");
    bool m_custom = false;
};

} // namespace pnq
