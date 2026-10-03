#include "io/FileFormats.h"
#include "core/Renderer.h"

#include <QFileInfo>
#include <QImageReader>
#include <QImageWriter>
#include <QPainter>
#include <QSaveFile>
#include <QtEndian>

namespace pnq {

QVector<FileFormat> FileFormats::all()
{
    static QVector<FileFormat> formats = [] {
        QVector<FileFormat> f;
        auto add = [&](const char* name, std::initializer_list<const char*> exts, bool alpha,
                       bool lossy, bool read, bool write, FileFormat::Kind kind = FileFormat::Kind::Raster,
                       bool multi = false) {
            FileFormat ff;
            ff.name = QString::fromLatin1(name);
            for (const char* e : exts)
                ff.extensions << QString::fromLatin1(e);
            ff.supportsAlpha = alpha;
            ff.lossy = lossy;
            ff.canRead = read;
            ff.canWrite = write;
            ff.kind = kind;
            ff.multiFrame = multi;
            f.append(ff);
        };
        add("Paint.NET Project", { "pdn" }, true, false, true, true, FileFormat::Kind::Project);
        add("PNG - Portable Network Graphics", { "png" }, true, false, true, true);
        add("JPEG - Joint Photographic Experts Group", { "jpg", "jpeg", "jpe", "jfif" }, false, true,
            true, true);
        add("BMP - Windows Bitmap", { "bmp", "dib" }, false, false, true, true);
        add("TIFF - Tagged Image File Format", { "tif", "tiff" }, true, false, true, true, FileFormat::Kind::Raster, true);
        add("WebP - Web Portable Format", { "webp" }, true, true, true, true);
        add("GIF - Graphics Interchange Format", { "gif" }, true, true, true, true, FileFormat::Kind::Raster, true);
        add("ICO - Windows Icon", { "ico", "cur" }, true, false, true, true);
        add("TGA - Truevision Targa", { "tga", "targa" }, true, false, false, false);
        add("PPM/PGM/PBM - Netpbm", { "ppm", "pgm", "pbm", "pnm" }, false, false, true, true);
        add("XPM - X11 Pixmap", { "xpm" }, true, false, true, true);
        add("JPG Large - JPEG XL quality", { "jpg" }, false, true, true, true);
        return f;
    }();
    return formats;
}

FileFormat FileFormats::byExtension(const QString& ext)
{
    const QString e = ext.toLower();
    for (const FileFormat& f : all()) {
        if (f.extensions.contains(e))
            return f;
    }
    return FileFormat();
}

QStringList FileFormats::openFilters(const QString& allFilter)
{
    QStringList filters;
    QStringList groups;
    for (const FileFormat& f : all()) {
        if (!f.canRead)
            continue;
        QStringList pats;
        for (const QString& e : f.extensions) {
            if (groups.contains(e) && f.kind == FileFormat::Kind::Project)
                continue;
            pats << QStringLiteral("*.") + e;
            groups << e;
        }
        if (pats.isEmpty())
            continue;
        filters << QStringLiteral("%1 (%2)").arg(f.name, pats.join(QLatin1Char(' ')));
    }
    filters << QStringLiteral("All image files (%1)").arg(openFilters().isEmpty() ? QString()
                                                                                : QString());
    if (!allFilter.isNull())
        filters << allFilter;
    return filters;
}

QStringList FileFormats::saveFilters()
{
    QStringList filters;
    for (const FileFormat& f : all()) {
        if (!f.canWrite)
            continue;
        QStringList pats;
        for (const QString& e : f.extensions)
            pats << QStringLiteral("*.") + e;
        if (pats.isEmpty())
            continue;
        if (filters.contains(QStringLiteral("%1 (%2)").arg(f.name, pats.join(QLatin1Char(' ')))))
            continue;
        filters << QStringLiteral("%1 (%2)").arg(f.name, pats.join(QLatin1Char(' ')));
    }
    return filters;
}

QStringList FileFormats::importFilters()
{
    QStringList filters;
    QStringList seen;
    for (const FileFormat& f : all()) {
        if (!f.canRead || f.kind == FileFormat::Kind::Project)
            continue;
        QStringList pats;
        for (const QString& e : f.extensions) {
            if (seen.contains(e))
                continue;
            pats << QStringLiteral("*.") + e;
            seen << e;
        }
        if (pats.isEmpty())
            continue;
        filters << QStringLiteral("%1 (%2)").arg(f.name, pats.join(QLatin1Char(' ')));
    }
    filters << QStringLiteral("All files (*)");
    return filters;
}

QString FileFormats::defaultExtensionFor(const Document& doc)
{
    if (doc.filePath().isEmpty())
        return QStringLiteral("png");
    const QString ext = QFileInfo(doc.filePath()).suffix().toLower();
    if (ext.isEmpty())
        return QStringLiteral("png");
    const FileFormat f = byExtension(ext);
    return (f.canWrite && f.kind == FileFormat::Kind::Raster) ? ext : QStringLiteral("png");
}

QImage FileFormats::exportImage(const Document& doc, bool forceFlatten)
{
    QImage img = doc.compositeImage();
    if (img.isNull())
        return img;
    img.setDotsPerMeterX(3780);
    img.setDotsPerMeterY(3780);
    if (forceFlatten) {
        QImage flat(img.size(), QImage::Format_RGB32);
        QPainter p(&flat);
        p.fillRect(flat.rect(), Qt::white);
        p.drawImage(0, 0, img);
        p.end();
        return flat;
    }
    return img;
}

static QString formatError(QImageWriter::ImageWriterError e)
{
    switch (e) {
    case QImageWriter::UnknownError: return QObject::tr("Unknown error");
    case QImageWriter::DeviceError: return QObject::tr("Error accessing the device");
    case QImageWriter::UnsupportedFormatError: return QObject::tr("Unsupported image format");
    case QImageWriter::InvalidImageError: return QObject::tr("Invalid image data");
    }
    return QObject::tr("Error");
}

bool FileFormats::saveImage(const QImage& image, const QString& path, const SaveOptions& options,
                            QString* error)
{
    if (image.isNull()) {
        if (error)
            *error = QObject::tr("No image data");
        return false;
    }
    const QString ext = QFileInfo(path).suffix().toLower();
    QImage img = image;
    if (!img.hasAlphaChannel() || !supportsTransparency(path))
        img = img.convertToFormat(QImage::Format_RGB32);

    QImageWriter writer(path);
    if (!writer.canWrite()) {
        if (error)
            *error = QObject::tr("Cannot write '%1'").arg(QFileInfo(path).fileName());
        return false;
    }
    if (ext == QLatin1String("jpg") || ext == QLatin1String("jpeg")) {
        writer.setFormat(QByteArray("jpeg"));
        writer.setQuality(options.jpegQuality);
        writer.setOptimizedWrite(true);
        writer.setProgressiveScanWrite(false);
        if (options.jpegSmooth) {
            // 4:4:4 chroma subsampling: no subsampling.
            writer.setSubType(QByteArray("444"));
        } else {
            writer.setSubType(QByteArray("420"));
        }
    } else if (ext == QLatin1String("png")) {
        writer.setFormat(QByteArray("png"));
        writer.setCompression(options.pngOptimize ? 9 : 1);
    } else if (ext == QLatin1String("bmp")) {
        writer.setFormat(QByteArray("bmp"));
        // 0 = no compression, 1 = RLE8
        writer.setCompression(options.bmpRle ? 1 : 0);
    } else if (ext == QLatin1String("webp")) {
        writer.setQuality(options.webpLossless ? 100 : options.webpQuality);
    }
    if (!writer.write(img)) {
        if (error)
            *error = formatError(writer.error());
        return false;
    }
    return true;
}

bool FileFormats::saveMultiPage(const QVector<QImage>& pages, const QString& path,
                                const SaveOptions& options, QString* error)
{
    const QString ext = QFileInfo(path).suffix().toLower();
    if ((ext == QLatin1String("tif") || ext == QLatin1String("tiff")) && pages.size() > 1) {
        // Multi page TIFF is written by chaining TIFF files is not supported by Qt;
        // fall back to writing the first page with a warning.
        QString warn = QObject::tr("Only the first page could be written");
        bool ok = saveImage(pages.first(), path, options, error);
        if (ok && error)
            *error = warn;
        return ok;
    }
    if (ext == QLatin1String("gif") && pages.size() > 1) {
        QImageWriter writer(path);
        writer.setFormat(QByteArray("gif"));
        for (const QImage& img : pages) {
            if (!writer.write(img)) {
                if (error)
                    *error = writer.errorString();
                return false;
            }
        }
        return true;
    }
    if (pages.isEmpty()) {
        if (error)
            *error = QObject::tr("No pages");
        return false;
    }
    return saveImage(pages.first(), path, options, error);
}

bool FileFormats::sizeAllowed(const QSize& size)
{
    return size.isValid() && !size.isEmpty() && size.width() <= MaxDimension
           && size.height() <= MaxDimension
           && qint64(size.width()) * qint64(size.height()) <= MaxPixels;
}

QVector<QImage> FileFormats::loadTiffPages(const QString& path, QString* error)
{
    QVector<QImage> pages;
    QImageReader reader(path);
    const int count = reader.imageCount();
    // imageCount() is a number out of the file. Left alone it is how many full
    // sized images this loop will hold in memory at once.
    const int pagesToRead = count > MaxFrames ? MaxFrames : count;
    for (int i = 0; i < pagesToRead; ++i) {
        if (!reader.jumpToNextImage())
            break;
        if (!sizeAllowed(reader.size())) {
            if (error)
                *error = QObject::tr("A page in this file is larger than this program will open");
            return QVector<QImage>();
        }
        QImage img = reader.read();
        if (img.isNull())
            break;
        pages.append(img);
    }
    return pages;
}

QImage FileFormats::load(const QString& path, QString* error)
{
    QImageReader reader(path);
    reader.setAutoTransform(true);
    // The size is in the header and costs nothing to ask for. Reading first and
    // looking afterwards is the wrong order: the memory is already gone.
    const QSize declared = reader.size();
    if (declared.isValid() && !declared.isEmpty() && !sizeAllowed(declared)) {
        if (error)
            *error = QObject::tr("This image is larger than this program will open");
        return QImage();
    }
    QImage img = reader.read();
    if (img.isNull() && error)
        *error = reader.errorString();
    if (img.isNull())
        return img;
    img.setDotsPerMeterX(3780);
    img.setDotsPerMeterY(3780);
    return img;
}

QVector<QImage> FileFormats::loadAllFrames(const QString& path, QString* error)
{
    const QString ext = QFileInfo(path).suffix().toLower();
    if (ext == QLatin1String("tif") || ext == QLatin1String("tiff"))
        return loadTiffPages(path, error);
    QImageReader reader(path);
    reader.setAutoTransform(true);
    QVector<QImage> frames;
    const int count = reader.imageCount();
    if (count > MaxFrames) {
        if (error)
            *error = QObject::tr("This file has more frames than this program will open");
        return QVector<QImage>();
    }
    if (count <= 1) {
        if (!sizeAllowed(reader.size())) {
            if (error)
                *error = QObject::tr("This image is larger than this program will open");
            return QVector<QImage>();
        }
        QImage img = reader.read();
        if (img.isNull() && error)
            *error = reader.errorString();
        if (!img.isNull())
            frames.append(img);
        return frames;
    }
    for (int i = 0; i < count; ++i) {
        if (i > 0 && !reader.jumpToNextImage())
            break;
        if (!sizeAllowed(reader.size())) {
            if (error)
                *error = QObject::tr("A frame in this file is larger than this program will open");
            return QVector<QImage>();
        }
        QImage img = reader.read();
        if (img.isNull())
            break;
        frames.append(img);
    }
    if (frames.isEmpty() && error)
        *error = reader.errorString();
    return frames;
}

bool FileFormats::isLossy(const QString& path)
{
    return byExtension(QFileInfo(path).suffix()).lossy;
}

bool FileFormats::supportsTransparency(const QString& path)
{
    return byExtension(QFileInfo(path).suffix()).supportsAlpha;
}

QString FileFormats::humanFileSize(const QString& path)
{
    const qint64 size = QFileInfo(path).size();
    if (size < 1024)
        return QStringLiteral("%1 B").arg(size);
    if (size < 1024 * 1024)
        return QStringLiteral("%1 KB").arg(double(size) / 1024.0, 0, 'f', 2);
    return QStringLiteral("%1 MB").arg(double(size) / (1024.0 * 1024.0), 0, 'f', 2);
}

} // namespace pnq
