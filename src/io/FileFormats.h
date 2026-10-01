#pragma once

#include "core/Document.h"
#include "core/Surface.h"

#include <QImage>
#include <QString>
#include <QStringList>
#include <QVector>

namespace pnq {

class FileFormat
{
public:
    enum class Kind { Raster, Project };

    QString name;
    QStringList extensions; ///< without the dot, lower case
    Kind kind = Kind::Raster;
    bool canRead = true;
    bool canWrite = true;
    bool supportsAlpha = true;
    bool lossy = false;
    bool multiFrame = false;
};

class SaveOptions
{
public:
    // JPEG
    int jpegQuality = 90;
    bool jpegSmooth = true;
    int jpegSubsampling = 0; ///< 0=4:4:4 1=4:2:2 2=4:2:0
    // PNG
    bool pngInterlaced = false;
    int pngBitDepth = 32; ///< 1,2,4,8,16,24,32 (24 drops alpha)
    bool pngOptimize = true;
    // BMP
    bool bmpRle = false;
    // TIFF
    int tiffCompression = 1;
    // WEBP
    int webpQuality = 90;
    bool webpLossless = false;
    // GIF
    int gifTransparency = 0;
    // ICO
    QVector<QSize> icoSizes;
};

class FileFormats
{
public:
    static QVector<FileFormat> all();
    static FileFormat byExtension(const QString& ext);
    static QStringList openFilters(const QString& allFilter = QString());
    static QStringList saveFilters();
    static QStringList importFilters();
    static QString defaultExtensionFor(const Document& doc);

    /// Returns the composited document image, flattened for formats without alpha.
    static QImage exportImage(const Document& doc, bool forceFlatten = false);

    /// Saves a document to `path`, picking the format from the extension.
    static bool save(Document& doc, const QString& path, const SaveOptions& options,
                     QString* error = nullptr);
    /// Saves an arbitrary image (used by "Copy to file"/export of selections).
    static bool saveImage(const QImage& image, const QString& path, const SaveOptions& options,
                          QString* error = nullptr);

    /// Loads an image; returns a null image on failure.
    static QImage load(const QString& path, QString* error = nullptr);
    /// Loads the first frame of a multi page document (TIFF pages are returned too).
    static QVector<QImage> loadAllFrames(const QString& path, QString* error = nullptr);

    /// Multi-page TIFF support.
    static bool saveMultiPage(const QVector<QImage>& pages, const QString& path,
                              const SaveOptions& options, QString* error = nullptr);
    static QVector<QImage> loadTiffPages(const QString& path);

    /// True when the document should be saved losslessly / when warnings apply.
    static bool isLossy(const QString& path);
    static bool supportsTransparency(const QString& path);
    /// Human readable file size of a saved file.
    static QString humanFileSize(const QString& path);
};

} // namespace pnq
