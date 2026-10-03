#pragma once

#include "core/Document.h"

#include <QByteArray>
#include <QString>

namespace pnq {

/// Reads Paint.NET's own project file, the one that starts with "PDN3".
///
/// This is a different format from the one PdqFile writes, which is JSON under
/// the .pdq extension. Paint.NET's file is a binary container:
///
///   "PDN3" | int32le xmlLength | xml of that length | \x00 \x01 | NRBF | pixels
///
/// The NRBF part is a .NET BinaryFormatter stream: a graph of typed objects with
/// an id table, so a member can be a reference to an object defined earlier rather
/// than a value. Reading it needs the whole stream, which is why this is a separate
/// class rather than a function next to PdqFile.
class Pdn3Reader
{
public:
    /// The largest canvas this will open, and the limits that keep a file from
    /// asking for memory the machine cannot give. Same numbers PdqFile uses, so a
    /// canvas means one thing whichever loader reads it.
    static constexpr int MaxDimension = 30000;
    static constexpr qint64 MaxPixels = 80'000'000;
    static constexpr int MaxLayers = 256;

    /// A ceiling on one document as a whole, not on one layer.
    ///
    /// The per-layer limits leave 256 layers of 80 megapixels between them, which is
    /// 80 gigabytes of decoded pixels, and a file holding that is small: runs of
    /// zeros in a gzip block shrink by roughly a thousand to one. So the limits that
    /// matter have to be on the sum. 200 megapixels is 800 MB of surface, which is
    /// far past anything a person draws and comfortably past a large photograph.
    static constexpr qint64 MaxTotalPixels = 200'000'000;

    /// True when the bytes begin with Paint.NET's signature. Cheap, and used to
    /// tell the two formats apart before either is parsed.
    static bool looksLikePaintNet(const QByteArray& data);

    /// Reads a Paint.NET file from disk. Returns nullptr and fills error on failure.
    static Document* load(const QString& path, QString* error = nullptr);

    /// The same, for bytes already in hand.
    static Document* loadFromData(const QByteArray& data, QString* error = nullptr);

    /// Turns one layer's stored bytes into a Surface.
    ///
    /// Paint.NET keeps pixel data as BGRA with the top row first. This program
    /// keeps ARGB with premultiplied alpha, so the conversion is what moves the
    /// channels across and takes the alpha out of the colour.
    static bool fillSurface(const QByteArray& pixels, qint64 length, int w, int h, Surface* out,
                            QString* error = nullptr);
};

} // namespace pnq
