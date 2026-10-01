#pragma once

#include "core/BlendMode.h"
#include "core/Surface.h"

#include <QImage>
#include <QJsonObject>
#include <QString>
#include <QVector>

namespace pnq {

enum class LayerLock {
    None = 0,
    TransparentPixels,
    BackgroundPixels,
    All
};

class Layer
{
public:
    explicit Layer(int docWidth, int docHeight);

    int width() const { return m_surface.width(); }
    int height() const { return m_surface.height(); }
    QRect bounds() const { return m_surface.bounds(); }
    bool isNull() const { return m_surface.isNull(); }

    Surface& surface() { return m_surface; }
    const Surface& surface() const { return m_surface; }
    void setSurface(const Surface& s) { m_surface = s; }

    QString name() const { return m_name; }
    void setName(const QString& n) { m_name = n; }

    bool visible() const { return m_visible; }
    void setVisible(bool v) { m_visible = v; }

    int opacity() const { return m_opacity; }
    void setOpacity(int o) { m_opacity = qBound(0, o, 255); }

    BlendMode blendMode() const { return m_blendMode; }
    void setBlendMode(BlendMode m) { m_blendMode = m; }

    LayerLock lock() const { return m_lock; }
    void setLock(LayerLock l) { m_lock = l; }

    bool isBackground() const { return m_isBackground; }
    void setBackground(bool b) { m_isBackground = b; }

    int id() const { return m_id; }
    void setId(int id) { m_id = id; }

    /// True when the layer can be painted on with the given selection mask pixel.
    bool canPaintAt(int x, int y) const;

    Surface& thumbnailSource() { m_thumbnailDirty = true; return m_surface; }
    bool thumbnailDirty() const { return m_thumbnailDirty; }
    void markThumbnailDirty() { m_thumbnailDirty = true; }
    void setThumbnailDirty(bool d) { m_thumbnailDirty = d; }

    /// A flattened thumbnail of this layer.
    QImage thumbnail(int maxSize) const;

    Layer* clone() const;

    /// Serialisation helpers.
    QJsonObject toJson(const QByteArray& encodedPixels) const;
    static Layer* fromJson(const QJsonObject& obj, const Surface& pixels, int docW, int docH);

private:
    Surface m_surface;
    QString m_name;
    bool m_visible = true;
    int m_opacity = 255;
    BlendMode m_blendMode = BlendMode::Normal;
    LayerLock m_lock = LayerLock::None;
    bool m_isBackground = false;
    int m_id = 0;
    mutable bool m_thumbnailDirty = true;
};

} // namespace pnq
