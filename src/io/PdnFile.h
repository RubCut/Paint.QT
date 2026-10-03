#pragma once

#include "core/Document.h"
#include "core/Selection.h"

#include <QByteArray>
#include <QJsonObject>
#include <QString>

namespace pnq {

/// Reads and writes the Paint.NET project format (.pdn, "magic":"pdn", v3).
///
/// This is not Paint.NET's own file format. Paint.NET writes a binary container
/// that starts with the four bytes "PDN3"; this writes a JSON document. Files
/// do not move between the two programs, in either direction, and the extension
/// is the only thing they share.
class PdnFile
{
public:
    static constexpr int CurrentVersion = 3;

    /// Limits applied to anything read from a file, because a file is input and
    /// the numbers in it are not to be trusted.
    ///
    /// Without these, a few kilobytes of JSON naming a 200000x200000 canvas ask
    /// for 160 GB per layer, and `w * h * 4` computed in int wraps around, so
    /// the check that should catch it passes instead. 80 megapixels is 320 MB a
    /// layer, which is far past anything a person draws.
    static constexpr int MaxDimension = 30000;
    static constexpr qint64 MaxPixels = 80'000'000;
    static constexpr int MaxLayers = 256;

    static bool save(const Document& doc, const QString& path, QString* error = nullptr);
    /// Creates a new document from a .pdn file. Returns nullptr on failure.
    static Document* load(const QString& path, QString* error = nullptr);
    static Document* loadFromData(const QByteArray& data, QString* error = nullptr);

    static QJsonObject documentToJson(const Document& doc);
    static QByteArray encodeLayerPixels(const Surface& s);
    static Surface decodeLayerPixels(const QByteArray& data, int w, int h, bool simple = true,
                                      QString* error = nullptr);
    static QByteArray encodeSelectionMask(const Selection& sel);
    static Selection decodeSelectionMask(const QByteArray& data, int w, int h,
                                         QString* error = nullptr);

    /// Selection files: a document whose only content is a selection. This is
    /// Paint.QT's own format as well, and Paint.NET does not read these either.
    static bool saveSelection(const Selection& sel, const QString& path, QString* error = nullptr);
    static bool loadSelection(const QString& path, Selection* out, QString* error = nullptr);
};

} // namespace pnq
