#include "io/PdnFile.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QTextStream>

namespace pnq {

namespace {

/// The number of bytes one layer of w by h pixels occupies, as a 64 bit value.
///
/// Every size that comes out of a file has to be computed this way. In int,
/// w * h * 4 wraps long before the allocation is refused, so the check that was
/// meant to stop it passes and the wrap decides the buffer size instead.
qint64 pixelBytes(int w, int h)
{
    return qint64(w) * qint64(h) * 4;
}

/// Whether a canvas of this size is one this program will attempt at all.
bool sizeAllowed(int w, int h)
{
    return w > 0 && h > 0 && w <= PdnFile::MaxDimension && h <= PdnFile::MaxDimension
           && qint64(w) * h <= PdnFile::MaxPixels;
}

/// Decompresses a layer, but only after checking what it claims to expand to.
///
/// qCompress stores the uncompressed length as the first four bytes, big endian,
/// so the claim can be read before anything is allocated. Without that, a short
/// buffer naming a huge size is expanded by qUncompress first and refused after,
/// which is the wrong order: the memory is already gone by then. A buffer that is
/// not compressed at all is accepted only when its length already matches.
QByteArray checkedUncompress(const QByteArray& data, qint64 expected, QString* error,
                             const QString& what)
{
    if (expected < 0 || expected > PdnFile::MaxPixels * 4) {
        if (error)
            *error = QObject::tr("%1 is larger than this program will open").arg(what);
        return QByteArray();
    }
    if (data.isEmpty())
        return QByteArray();
    if (data.size() == expected) {
        QByteArray raw = data;
        return raw;
    }
    if (data.size() < 4) {
        if (error)
            *error = QObject::tr("%1 is truncated").arg(what);
        return QByteArray();
    }
    const auto* p = reinterpret_cast<const uchar*>(data.constData());
    const qint64 declared = (qint64(p[0]) << 24) | (qint64(p[1]) << 16)
                            | (qint64(p[2]) << 8) | qint64(p[3]);
    if (declared != expected) {
        if (error)
            *error = QObject::tr("%1 does not match the canvas size").arg(what);
        return QByteArray();
    }
    QByteArray raw = qUncompress(data);
    if (raw.size() != expected) {
        if (error)
            *error = QObject::tr("%1 could not be decompressed").arg(what);
        return QByteArray();
    }
    return raw;
}

} // namespace

QByteArray PdnFile::encodeLayerPixels(const Surface& s)
{
    // Paint.NET stores raw scanlines, pixel order B,G,R,A for the "simple" case.
    const int w = s.width(), h = s.height();
    QByteArray raw;
    raw.resize(pixelBytes(w, h));
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

Surface PdnFile::decodeLayerPixels(const QByteArray& data, int w, int h, bool simple, QString* error)
{
    if (!sizeAllowed(w, h)) {
        if (error)
            *error = QObject::tr("Invalid image dimensions");
        return Surface();
    }
    const qint64 expected = pixelBytes(w, h);
    const QByteArray raw = checkedUncompress(data, expected, error, QObject::tr("The layer"));
    if (raw.isEmpty())
        return Surface();
    Surface s(w, h);
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

Selection PdnFile::decodeSelectionMask(const QByteArray& data, int w, int h, QString* error)
{
    if (!sizeAllowed(w, h)) {
        if (error)
            *error = QObject::tr("Invalid image dimensions");
        return Selection();
    }
    // The mask is one byte per pixel, not four like a layer.
    const qint64 expected = qint64(w) * qint64(h);
    const QByteArray raw = checkedUncompress(data, expected, error, QObject::tr("The selection"));
    if (raw.isEmpty())
        return Selection();
    QImage mask(w, h, QImage::Format_Alpha8);
    mask.fill(0);
    if (qint64(mask.sizeInBytes()) == raw.size())
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
            *error = QObject::tr("Not a Paint.QT file");
        return nullptr;
    }
    const QJsonObject root = jd.object();
    if (root.value(QStringLiteral("magic")).toString() != QLatin1String("pdn")) {
        if (error)
            *error = QObject::tr("Not a Paint.QT file");
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
    if (!sizeAllowed(w, h)) {
        if (error)
            *error = QObject::tr("Invalid image dimensions");
        return nullptr;
    }

    // The layer count is read from the file too. Each layer is a full canvas,
    // so a file naming a few thousand of them asks for a few thousand of them.
    const QJsonArray layers = root.value(QStringLiteral("layers")).toArray();
    if (layers.size() > MaxLayers) {
        if (error)
            *error = QObject::tr("This file has more layers than this program will open");
        return nullptr;
    }

    Document* doc = new Document(w, h);
    for (const QJsonValue& v : layers) {
        const QJsonObject lo = v.toObject();
        const QByteArray pixels =
            QByteArray::fromBase64(lo.value(QStringLiteral("pixels")).toString().toLatin1());
        QString layerError;
        const Surface surf = decodeLayerPixels(pixels, w, h, true, &layerError);
        if (!layerError.isEmpty()) {
            delete doc;
            if (error)
                *error = layerError;
            return nullptr;
        }
        Layer* l = Layer::fromJson(lo, surf, w, h);
        doc->insertLayer(l, -1);
    }
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
    // These two numbers come straight out of the file and used to be handed to
    // QImage unchecked, so a mask of 200000 by 200000 was allocated before
    // anything was compared against it.
    const int mw = root.value(QStringLiteral("width")).toInt();
    const int mh = root.value(QStringLiteral("height")).toInt();
    if (!sizeAllowed(mw, mh)) {
        if (error)
            *error = QObject::tr("Invalid image dimensions");
        return false;
    }
    const QByteArray raw = checkedUncompress(
        QByteArray::fromBase64(root.value(QStringLiteral("selection")).toString().toLatin1()),
        qint64(mw) * qint64(mh), error, QObject::tr("The selection"));
    if (raw.isEmpty())
        return false;
    QImage mask(mw, mh, QImage::Format_Alpha8);
    if (qint64(mask.sizeInBytes()) == raw.size())
        memcpy(mask.bits(), raw.constData(), raw.size());
    Selection s;
    s.setMask(mask);
    if (out)
        *out = s;
    return true;
}

} // namespace pnq
