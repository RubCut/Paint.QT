// Renders the application mark to every file the packaging needs: PNGs at the
// sizes the icon theme spec asks for, a Windows .ico, and an Apple .icns.
//
// Rendering goes through Qt's SVG rasteriser rather than an external tool, so
// the packaged images are produced by exactly the same rasteriser the test that
// compares the SVG against the in-app drawing uses. That means the files that
// ship cannot drift from the icon the window shows.
//
// Usage: pnq_make_icons <svg> <output directory>
#include <QCoreApplication>
#include <QImage>
#include <QPainter>
#include <QSvgRenderer>

#include <QBuffer>
#include <QByteArray>
#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QVector>

#include <cstdio>

namespace {

/// PNG bytes at the given size.
QByteArray renderPng(QSvgRenderer& svg, int size)
{
    QImage img(size, size, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    QPainter p(&img);
    svg.render(&p, QRectF(0, 0, size, size));
    p.end();
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    img.save(&buffer, "PNG");
    return bytes;
}

/// A Windows .ico holding PNG compressed entries, which every Windows since
/// Vista reads and which keeps the file small even at 256 px.
bool writeIco(const QString& path, const QVector<int>& sizes, QSvgRenderer& svg)
{
    QVector<QByteArray> images;
    QVector<int> used;
    for (const int s : sizes) {
        const QByteArray png = renderPng(svg, s);
        // A size already present would be a second entry pointing at the same
        // pixels; skip it rather than store it twice.
        if (images.contains(png))
            continue;
        images.append(png);
        used.append(s);
    }
    if (images.isEmpty())
        return false;

    // The directory needs each entry's offset, and the offsets are only known
    // once every image's length is. Build the header with placeholders, then
    // fill them in.
    const quint32 header = 6 + 16 * quint32(images.size());
    QVector<quint32> offsets;
    quint32 at = header;
    for (const QByteArray& b : images) {
        offsets.append(at);
        at += quint32(b.size());
    }

    QByteArray blob;
    QDataStream out(&blob, QIODevice::WriteOnly);
    out.setByteOrder(QDataStream::LittleEndian);
    out << quint16(0) << quint16(1) << quint16(images.size());
    for (int i = 0; i < images.size(); ++i) {
        const int s = used.at(i);
        // Zero means 256 in the ICO directory, there is no room for the value.
        const quint8 dim = s >= 256 ? 0 : quint8(s);
        out << dim << dim << quint8(0) << quint8(0);
        out << quint16(1) << quint16(32);
        out << quint32(images.at(i).size());
        out << offsets.at(i);
    }
    // The image payloads go in raw. QDataStream would prefix each QByteArray
    // with a 4 byte length, which is not part of the ICO format.
    out.device()->seek(header);

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return false;
    f.write(blob);
    for (const QByteArray& b : std::as_const(images))
        f.write(b);
    f.close();
    return true;
}

/// An Apple .icns. The element types are the "ic07"/"ic08"/... families, which
/// cover 128 through 1024 and are what modern macOS expects.
bool writeIcns(const QString& path, QSvgRenderer& svg)
{
    struct Entry {
        const char* type;
        int size;
    };
    // Only the sizes a real product ships; 1024 would bloat the file for nothing.
    const Entry entries[] = { { "icp4", 16 },  { "icp5", 32 },   { "icp6", 64 }, { "ic07", 128 },
                              { "ic08", 256 }, { "ic09", 512 }, { "ic10", 1024 } };
    QVector<QByteArray> images;
    for (const Entry& e : entries)
        images.append(renderPng(svg, e.size));

    // Element headers are written by hand into the buffer. QDataStream would
    // prefix a QByteArray with a 4 byte length, which would corrupt the file.
    QByteArray body;
    for (int i = 0; i < images.size(); ++i) {
        const char* type = entries[i].type;
        for (int k = 0; k < 4; ++k)
            body.append(type[k]);
        for (int k = 3; k >= 0; --k)
            body.append(char(quint32(images.at(i).size() + 8) >> (8 * k)));
        body.append(images.at(i));
    }

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return false;
    const char magic[4] = { 'i', 'c', 'n', 's' };
    f.write(magic, 4);
    const quint32 total = quint32(body.size() + 8);
    for (int k = 3; k >= 0; --k)
        f.putChar(char(total >> (8 * k)));
    f.write(body);
    f.close();
    return true;
}

} // namespace

int main(int argc, char** argv)
{
    // QCoreApplication on purpose. This tool rasterises the mark into QImages and
    // never opens a window, so it must not need a display: it runs as part of the
    // build, and a build machine has none. A QGuiApplication here aborts with
    // "could not connect to display" and takes the whole build down with it.
    QCoreApplication app(argc, argv);
    if (argc < 3) {
        std::fprintf(stderr, "usage: %s <svg> <output dir>\n", argv[0]);
        return 2;
    }
    const QString svgPath = QString::fromLocal8Bit(argv[1]);
    const QString outDir = QString::fromLocal8Bit(argv[2]);
    QDir().mkpath(outDir);

    QSvgRenderer svg(svgPath);
    if (!svg.isValid()) {
        std::fprintf(stderr, "cannot render %s\n", qPrintable(svgPath));
        return 1;
    }

    // The sizes the freedesktop icon theme spec asks for, plus 512 for anyone who
    // wants a large one.
    const int sizes[] = { 16, 24, 32, 48, 64, 128, 256, 512 };
    int written = 0;
    for (const int s : sizes) {
        const QString path = QStringLiteral("%1/paint-qt-%2.png").arg(outDir).arg(s);
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly))
            continue;
        f.write(renderPng(svg, s));
        f.close();
        ++written;
    }
    if (!writeIco(QStringLiteral("%1/paint-qt.ico").arg(outDir),
                  { 16, 24, 32, 48, 64, 128, 256 }, svg))
        std::fprintf(stderr, "warning: could not write the .ico\n");
    if (!writeIcns(QStringLiteral("%1/paint-qt.icns").arg(outDir), svg))
        std::fprintf(stderr, "warning: could not write the .icns\n");

    std::printf("wrote %d PNGs, one .ico and one .icns into %s\n", written, qPrintable(outDir));
    return written > 0 ? 0 : 1;
}