#pragma once

#include "core/Document.h"
#include "core/Selection.h"

#include <QByteArray>
#include <QJsonObject>
#include <QString>

namespace pnq {

/// Reads and writes the Paint.NET project format (.pdn, "magic":"pdn", v3).
class PdnFile
{
public:
    static constexpr int CurrentVersion = 3;

    static bool save(const Document& doc, const QString& path, QString* error = nullptr);
    /// Creates a new document from a .pdn file. Returns nullptr on failure.
    static Document* load(const QString& path, QString* error = nullptr);
    static Document* loadFromData(const QByteArray& data, QString* error = nullptr);

    static QJsonObject documentToJson(const Document& doc);
    static QByteArray encodeLayerPixels(const Surface& s);
    static Surface decodeLayerPixels(const QByteArray& data, int w, int h, bool simple = true);
    static QByteArray encodeSelectionMask(const Selection& sel);
    static Selection decodeSelectionMask(const QByteArray& data, int w, int h);

    /// Selection files (Paint.NET compatible: a document with a selection).
    static bool saveSelection(const Selection& sel, const QString& path, QString* error = nullptr);
    static bool loadSelection(const QString& path, Selection* out, QString* error = nullptr);
};

} // namespace pnq
