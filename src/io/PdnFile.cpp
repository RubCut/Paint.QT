#include "io/PdnFile.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QTextStream>

namespace pnq {

QByteArray PdnFile::encodeLayerPixels(const Surface& s)
{
    // Paint.NET stores raw scanlines, pixel order B,G,R,A for the "simple" case.
    const int w = s.width(), h = s.height();
    QByteArray raw;
    raw.resize(w * h * 4);
    char* out = raw.data();
    for (int y = 0; y < h; ++y) {
        const pixel_t* row = s.scanLine(y);
        for (int x = 0; x < w; ++x) {
            const quint32 p = row[x];
            *out++ = char(p & 0xFF);         // B
            *out++ = char((p >> 8) & 0xFF);  // G
            *out++ = char((p >> 16) & 0xFF); // R
            *out++ = char((p >> 24) & 0xFF); // A
        }
    }
    return qCompress(raw, 9);
}

Surface PdnFile::decodeLayerPixels(const QByteArray& data, int w, int h, bool simple)
{
    Surface s(w, h);
    if (data.isEmpty())
        return s;
    QByteArray raw = qUncompress(data);
    if (raw.size() != w * h * 4) {
        // Some files store a raw (uncompressed) buffer.
        if (data.size() == w * h * 4)
            raw = data;
        else
            return s;
    }
    const char* in = raw.constData();
    for (int y = 0; y < h; ++y) {
        pixel_t* row = s.scanLine(y);
        for (int x = 0; x < w; ++x) {
            const quint8 b = quint8(*in++);
            const quint8 g = quint8(*in++);
            const quint8 r = quint8(*in++);
            const quint8 a = quint8(*in++);
            // The stored bytes are already premultiplied, so the pixel is
            // reassembled as-is; premultiplying again would darken it.
            row[x] = (quint32(a) << 24) | (quint32(r) << 16) | (quint32(g) << 8) | quint32(b);
        }
    }
    Q_UNUSED(simple);
    return s;
}

QJsonObject PdnFile::documentToJson(const Document& doc)
{
    QJsonObject root;
    root[QStringLiteral("magic")] = QStringLiteral("pdn");
    root[QStringLiteral("version")] = CurrentVersion;
    root[QStringLiteral("width")] = doc.width();
    root[QStringLiteral("height")] = doc.height();

    QJsonArray layers;
    for (int i = 0; i < doc.layerCount(); ++i) {
        Layer* l = doc.layerAt(i);
        QJsonObject lo;
        lo[QStringLiteral("name")] = l->name();
        lo[QStringLiteral("visible")] = l->visible();
        lo[QStringLiteral("opacity")] = double(l->opacity());
        lo[QStringLiteral("blendmode")] = blendModeKey(l->blendMode());
        lo[QStringLiteral("locked")] = int(l->lock());
        lo[QStringLiteral("isBackground")] = l->isBackground();
        lo[QStringLiteral("pixels")] =
            QString::fromLatin1(encodeLayerPixels(l->surface()).toBase64());
        layers.append(lo);
    }
    root[QStringLiteral("layers")] = layers;
    return root;
}

bool PdnFile::save(const Document& doc, const QString& path, QString* error)
{
    if (doc.width() <= 0 || doc.height() <= 0) {
        if (error)
            *error = QObject::tr("The image is empty");
        return false;
    }
    QJsonObject root = documentToJson(doc);
    if (!doc.selection().isNull()) {
        root[QStringLiteral("selection")] = QString::fromLatin1(encodeSelectionMask(doc.selection()).toBase64());
    }
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly)) {
        if (error)
            *error = f.errorString();
        return false;
    }
    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    if (!f.commit()) {
        if (error)
            *error = f.errorString();
        return false;
    }
    return true;
}

QByteArray PdnFile::encodeSelectionMask(const Selection& sel)
{
    if (sel.isNull())
        return QByteArray();
    QImage m = sel.mask();
    return qCompress(QByteArray(reinterpret_cast<const char*>(m.constBits()), m.sizeInBytes()), 9);
}

Selection PdnFile::decodeSelectionMask(const QByteArray& data, int w, int h)
{
    QImage mask(w, h, QImage::Format_Alpha8);
    mask.fill(0);
    if (data.isEmpty())
        return Selection();
    QByteArray raw = qUncompress(data);
    if (raw.size() == mask.sizeInBytes())
        memcpy(mask.bits(), raw.constData(), raw.size());
    Selection s;
    s.setMask(mask);
    return s;
}

Document* PdnFile::loadFromData(const QByteArray& data, QString* error)
{
    QJsonParseError err{};
    const QJsonDocument jd = QJsonDocument::fromJson(data, &err);
    if (err.error != QJsonParseError::NoError) {
        if (error)
            *error = err.errorString();
        return nullptr;
    }
    if (!jd.isObject()) {
        if (error)
            *error = QObject::tr("Not a Paint.NET file");
        return nullptr;
    }
    const QJsonObject root = jd.object();
    if (root.value(QStringLiteral("magic")).toString() != QLatin1String("pdn")) {
        if (error)
            *error = QObject::tr("Not a Paint.NET file");
        return nullptr;
    }
    const int version = root.value(QStringLiteral("version")).toInt(0);
    if (version < 1 || version > CurrentVersion) {
        if (error)
            *error = QObject::tr("Unsupported file version %1").arg(version);
        return nullptr;
    }
    const int w = root.value(QStringLiteral("width")).toInt();
    const int h = root.value(QStringLiteral("height")).toInt();
    if (w <= 0 || h <= 0 || w > 200000 || h > 200000) {
        if (error)
            *error = QObject::tr("Invalid image dimensions");
        return nullptr;
    }

    Document* doc = new Document(w, h);
    const QJsonArray layers = root.value(QStringLiteral("layers")).toArray();
    for (const QJsonValue& v : layers) {
        const QJsonObject lo = v.toObject();
        const QByteArray pixels =
            QByteArray::fromBase64(lo.value(QStringLiteral("pixels")).toString().toLatin1());
        const Surface surf = decodeLayerPixels(pixels, w, h);
        Layer* l = Layer::fromJson(lo, surf, w, h);
        doc->insertLayer(l, -1);    }
    if (doc->layerCount() == 0) {
        doc->addLayer();
    }
    // A saved selection (optional extension of the format).
    if (root.contains(QStringLiteral("selection"))) {
        doc->setSelection(decodeSelectionMask(
            QByteArray::fromBase64(root.value(QStringLiteral("selection")).toString().toLatin1()), w, h));
    }
    doc->setDirty(false);
    return doc;
}

Document* PdnFile::load(const QString& path, QString* error)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error)
            *error = f.errorString();
        return nullptr;
    }
    return loadFromData(f.readAll(), error);
}

bool PdnFile::saveSelection(const Selection& sel, const QString& path, QString* error)
{
    if (sel.isNull()) {
        if (error)
            *error = QObject::tr("There is no selection to save");
        return false;
    }
    QJsonObject root;
    root[QStringLiteral("magic")] = QStringLiteral("pdn");
    root[QStringLiteral("version")] = CurrentVersion;
    root[QStringLiteral("width")] = sel.width();
    root[QStringLiteral("height")] = sel.height();
    root[QStringLiteral("selection")] = QString::fromLatin1(encodeSelectionMask(sel).toBase64());
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly)) {
        if (error)
            *error = f.errorString();
        return false;
    }
    f.write(QJsonDocument(root).toJson());
    return f.commit();
}

bool PdnFile::loadSelection(const QString& path, Selection* out, QString* error)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error)
            *error = f.errorString();
        return false;
    }
    QJsonParseError err{};
    const QJsonDocument jd = QJsonDocument::fromJson(f.readAll(), &err);
    if (err.error != QJsonParseError::NoError || !jd.isObject()) {
        if (error)
            *error = QObject::tr("Invalid selection file");
        return false;
    }
    const QJsonObject root = jd.object();
    if (!root.contains(QStringLiteral("selection"))) {
        if (error)
            *error = QObject::tr("The file does not contain a selection");
        return false;
    }
    const QByteArray raw = qUncompress(QByteArray::fromBase64(
        root.value(QStringLiteral("selection")).toString().toLatin1()));
    QImage mask(root.value(QStringLiteral("width")).toInt(), root.value(QStringLiteral("height")).toInt(),
                QImage::Format_Alpha8);
    if (raw.size() == mask.sizeInBytes())
        memcpy(mask.bits(), raw.constData(), raw.size());
    Selection s;
    s.setMask(mask);
    if (out)
        *out = s;
    return true;
}

} // namespace pnq
