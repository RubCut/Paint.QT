#include "core/Layer.h"
#include "core/ColorUtils.h"

#include <QJsonObject>
#include <QPainter>

namespace pnq {

namespace {
int g_nextLayerId = 1;
}

Layer::Layer(int docWidth, int docHeight)
    : m_surface(docWidth, docHeight)
    , m_name(QObject::tr("Layer %1").arg(g_nextLayerId))
    , m_id(g_nextLayerId++)
{
}

bool Layer::canPaintAt(int x, int y) const
{
    switch (m_lock) {
    case LayerLock::None:
        return true;
    case LayerLock::All:
        return false;
    case LayerLock::TransparentPixels: {
        quint8 a = getA(m_surface.pixel(x, y));
        return a == 0;
    }
    case LayerLock::BackgroundPixels: {
        quint8 a = getA(m_surface.pixel(x, y));
        return a == 255;
    }
    }
    return true;
}

QImage Layer::thumbnail(int maxSize) const
{
    QImage src = m_surface.toQImage();
    if (src.isNull())
        return QImage();
    int w = src.width(), h = src.height();
    if (w <= maxSize && h <= maxSize)
        return src;
    double s = double(maxSize) / qMax(w, h);
    QImage t = src.scaled(qMax(1, int(w * s + 0.5)), qMax(1, int(h * s + 0.5)), Qt::IgnoreAspectRatio,
                          Qt::SmoothTransformation);
    return t;
}

Layer* Layer::clone() const
{
    Layer* l = new Layer(*this);
    l->m_surface = m_surface.copy();
    l->m_id = g_nextLayerId++;
    return l;
}

QJsonObject Layer::toJson(const QByteArray& encodedPixels) const
{
    QJsonObject o;
    o[QStringLiteral("name")] = m_name;
    o[QStringLiteral("visible")] = m_visible;
    o[QStringLiteral("opacity")] = double(m_opacity);
    o[QStringLiteral("blendmode")] = blendModeKey(m_blendMode);
    o[QStringLiteral("locked")] = int(m_lock);
    o[QStringLiteral("isBackground")] = m_isBackground;
    o[QStringLiteral("pixels")] = QString::fromLatin1(encodedPixels.toBase64());
    return o;
}

Layer* Layer::fromJson(const QJsonObject& obj, const Surface& pixels, int docW, int docH)
{
    Layer* l = new Layer(docW, docH);
    l->m_name = obj.value(QStringLiteral("name")).toString();
    l->m_visible = obj.value(QStringLiteral("visible")).toBool(true);
    l->m_opacity = qBound(0, int(obj.value(QStringLiteral("opacity")).toDouble(255)), 255);
    blendModeFromKey(obj.value(QStringLiteral("blendmode")).toString(), &l->m_blendMode);
    l->m_lock = static_cast<LayerLock>(int(obj.value(QStringLiteral("locked")).toDouble(0)));
    l->m_isBackground = obj.value(QStringLiteral("isBackground")).toBool(false);
    l->m_surface = pixels.isNull() ? Surface(docW, docH) : pixels;
    return l;
}

} // namespace pnq
