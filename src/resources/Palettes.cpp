#include "resources/Palettes.h"

#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSaveFile>
#include <QTextStream>
#include <algorithm>
#include <cmath>

namespace pnq {
namespace Palettes {

namespace {
Palette make(const QString& name, const QVector<int>& hexes)
{
    Palette p;
    p.name = name;
    p.colors.reserve(hexes.size());
    for (int v : hexes)
        p.colors.append(rgb(v));
    return p;
}

QVector<int> ramp(int from, int to, int count)
{
    QVector<int> out;
    const int fr = (from >> 16) & 0xFF, fg = (from >> 8) & 0xFF, fb = from & 0xFF;
    const int tr = (to >> 16) & 0xFF, tg = (to >> 8) & 0xFF, tb = to & 0xFF;
    for (int i = 0; i < count; ++i) {
        const double t = count > 1 ? double(i) / (count - 1) : 0.0;
        const int r = qRound(fr + t * (tr - fr));
        const int g = qRound(fg + t * (tg - fg));
        const int b = qRound(fb + t * (tb - fb));
        out.append((r << 16) | (g << 8) | b);
    }
    return out;
}
} // namespace

Palette web()
{
    // 6 x 6 x 6 colour cube plus a 6 step greyscale ramp = 216 colours.
    QVector<int> hexes;
    const int steps[6] = { 0, 51, 102, 153, 204, 255 };
    for (int r = 0; r < 6; ++r)
        for (int g = 0; g < 6; ++g)
            for (int b = 0; b < 6; ++b)
                hexes.append((steps[r] << 16) | (steps[g] << 8) | steps[b]);
    return make(QObject::tr("Web"), hexes); // 6 * 6 * 6 == 216
}

Palette grayscale(int count)
{
    QVector<int> hexes;
    for (int i = 0; i < count; ++i) {
        const int v = count > 1 ? qRound(double(i) * 255.0 / (count - 1)) : 0;
        hexes.append((v << 16) | (v << 8) | v);
    }
    return make(QObject::tr("Grayscale"), hexes);
}

Palette nes()
{
    static const int t[64] = {
        0x666666, 0x002A88, 0x1412A7, 0x3B00A4, 0x5C007E, 0x6E0040, 0x6C0600, 0x561D00,
        0x333500, 0x0B4800, 0x005200, 0x004F08, 0x00404D, 0x000000, 0x000000, 0x000000,
        0xADADAD, 0x155FD9, 0x4240FF, 0x7527FE, 0xA01ACC, 0xB71E7B, 0xB53120, 0x994E00,
        0x6B6D00, 0x388700, 0x0C9300, 0x008F32, 0x007C8D, 0x000000, 0x000000, 0x000000,
        0xFFFEFF, 0x64B0FF, 0x9290FF, 0xC676FF, 0xF36AFF, 0xFE6ECC, 0xFE8170, 0xEA9E22,
        0xBCBE00, 0x88D800, 0x5CE430, 0x45E082, 0x48CDDE, 0x4F4F4F, 0x000000, 0x000000,
        0xFFFEFF, 0xC0DFFF, 0xD3D2FF, 0xE8C8FF, 0xFBC2FF, 0xFEC4EA, 0xFECCC5, 0xF7D8A5,
        0xE4E594, 0xCFEF96, 0xBDF4AB, 0xB3F3CC, 0xB5EBF2, 0xB8B8B8, 0x000000, 0x000000
    };
    QVector<int> hexes;
    for (int v : t)
        hexes.append(v);
    return make(QStringLiteral("NES"), hexes);
}

Palette gameBoy()
{
    QVector<int> hexes = { 0x0F380F, 0x306230, 0x8BAC0F, 0x9BBC0F };
    return make(QStringLiteral("Game Boy"), hexes);
}

Palette material(int count)
{
    static const int m[] = { 0xF44336, 0xE91E63, 0x9C27B0, 0x673AB7, 0x3F51B5, 0x2196F3, 0x03A9F4,
                             0x00BCD4, 0x009688, 0x4CAF50, 0x8BC34A, 0xCDDC39, 0xFFEB3B, 0xFFC107,
                             0xFF9800, 0xFF5722, 0x795548, 0x9E9E9E, 0x607D8B };
    QVector<int> hexes;
    for (int i = 0; i < qMin(count, 19); ++i)
        hexes.append(m[i]);
    return make(QObject::tr("Material"), hexes);
}

Palette pastels()
{
    QVector<int> hexes = { 0xFFB3BA, 0xFFDFBA, 0xFFFFBA, 0xBAFFC9, 0xBAE1FF, 0xE6B3FF, 0xD9D9D9,
                           0xFFC0CB, 0xFFE4E1, 0xF0E68C, 0xE0FFE0, 0xD0F0FF, 0xF0D0FF, 0xFFFFFF };
    return make(QObject::tr("Pastels"), hexes);
}

Palette neon()
{
    QVector<int> hexes = { 0x00FF00, 0x00FF66, 0x00FFFF, 0x0066FF, 0x6600FF, 0xFF00FF, 0xFF0066,
                           0xFF0000, 0xFF6600, 0xFFFF00, 0x66FF00, 0xFFFFFF, 0x888888, 0x000000 };
    return make(QObject::tr("Neon"), hexes);
}

Palette earthTones()
{
    QVector<int> hexes = { 0x3B2F2F, 0x5C4033, 0x7B3F00, 0x8B5A2B, 0xA0522D, 0xC19A6B, 0xD2B48C,
                           0xDEB887, 0x6B8E23, 0x556B2F, 0x808000, 0x8FBC8F, 0x2E8B57, 0x1B4D3E };
    return make(QObject::tr("Earth Tones"), hexes);
}

Palette windows()
{
    QVector<int> hexes = { 0x000000, 0x800000, 0x008000, 0x808000, 0x000080, 0x800080, 0x008080,
                           0xC0C0C0, 0x808080, 0xFF0000, 0x00FF00, 0xFFFF00, 0x0000FF, 0xFF00FF,
                           0x00FFFF, 0xFFFFFF };
    return make(QStringLiteral("Windows"), hexes);
}

Palette autumn()
{
    QVector<int> hexes = { 0x7A3B00, 0x8B4000, 0xA0522D, 0xB5651D, 0xCD853F, 0xD2691E, 0xE97451,
                           0x8B0000, 0xA52A2A, 0xB22222, 0xC1440E, 0xD95F02, 0xE67E22, 0xF4A460 };
    return make(QObject::tr("Autumn"), hexes);
}

Palette forest()
{
    QVector<int> hexes = { 0x0B3D0B, 0x145A14, 0x1E7B1E, 0x2E8B57, 0x3CB371, 0x228B22, 0x006400,
                           0x556B2F, 0x6B8E23, 0x8FBC8F, 0x9ACD32, 0x7CFC00, 0xADFF2F, 0x00FF00 };
    return make(QObject::tr("Forest"), hexes);
}

Palette ocean()
{
    QVector<int> hexes = { 0x000080, 0x0000A0, 0x0000C0, 0x0000FF, 0x0080FF, 0x00A0C0, 0x008080,
                           0x006064, 0x004040, 0x20B2AA, 0x40E0D0, 0x48D1CC, 0x5F9EA0, 0x2E8B57 };
    return make(QObject::tr("Ocean"), hexes);
}

Palette metallik()
{
    QVector<int> hexes = { 0x000000, 0x1C1C1C, 0x383838, 0x545454, 0x707070, 0x8C8C8C, 0xA8A8A8,
                           0xC4C4C4, 0xE0E0E0, 0xFFFFFF, 0x7F6000, 0xB8860B, 0xFFD700, 0xC0C0C0 };
    return make(QStringLiteral("Metallik"), hexes);
}

QVector<Palette> all()
{
    return { web(),    nes(),        gameBoy(),     material(),  grayscale(),
             pastels(), neon(),    earthTones(),  windows(),   autumn(),
             forest(), ocean(),     metallik() };
}

Palette byName(const QString& name)
{
    for (const Palette& p : all())
        if (p.name.compare(name, Qt::CaseInsensitive) == 0)
            return p;
    return Palette();
}

bool loadFile(const QString& path, Palette* out, QString* error)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (error)
            *error = f.errorString();
        return false;
    }
    QTextStream in(&f);
    const QString head = in.readLine();
    Palette p;
    p.name = QFileInfo(path).completeBaseName();
    if (head.contains(QLatin1String("GIMP Palette"), Qt::CaseInsensitive)) {
        QString line;
        while (!in.atEnd()) {
            line = in.readLine().trimmed();
            if (line.isEmpty() || line.startsWith(QLatin1Char('#')) || line.startsWith(QLatin1Char(';')))
                continue;
            if (line.startsWith(QLatin1String("Name:"))) {
                p.name = line.mid(5).trimmed();
                continue;
            }
            const QStringList parts = line.split(QRegularExpression(QStringLiteral("\\s+")),
                                                Qt::SkipEmptyParts);
            if (parts.size() >= 3) {
                p.colors.append(qPremult(255, quint8(parts[0].toInt()), quint8(parts[1].toInt()),
                                         quint8(parts[2].toInt())));
            }
        }
    } else {
        QString line;
        int lineno = 0;
        while (!in.atEnd()) {
            line = in.readLine().trimmed();
            ++lineno;
            if (line.isEmpty() || line.startsWith(QLatin1Char(';')))
                continue;
            QString hex = line;
            if (!hex.startsWith(QLatin1Char('#')))
                hex.prepend(QLatin1Char('#'));
            bool ok = false;
            const pixel_t c = colorFromHex(hex, &ok);
            if (ok) {
                p.colors.append(c);
            } else if (lineno < 3) {
                if (error)
                    *error = QObject::tr("'%1' is not a palette file").arg(QFileInfo(path).fileName());
                return false;
            }
        }
    }
    if (p.colors.isEmpty()) {
        if (error)
            *error = QObject::tr("The palette is empty");
        return false;
    }
    if (out)
        *out = p;
    return true;
}

bool saveFile(const Palette& pal, const QString& path, QString* error)
{
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (error)
            *error = f.errorString();
        return false;
    }
    QTextStream out(&f);
    const QString ext = QFileInfo(path).suffix().toLower();
    if (ext == QLatin1String("gpl")) {
        out << "GIMP Palette\n";
        out << "Name: " << pal.name << "\n";
        out << "Columns: 8\n";
        out << "#\n";
        for (pixel_t c : pal.colors)
            out << getR(c) << " " << getG(c) << " " << getB(c) << "\t" << colorName(c) << "\n";
    } else {
        out << ";Paint.NET Palette File\n";
        out << ";Colors: " << pal.colors.size() << "\n";
        for (pixel_t c : pal.colors)
            out << colorToHex(c).mid(1) << "\n";
    }
    return f.commit();
}

int nearest(const Palette& p, pixel_t c)
{
    int best = -1;
    int bestDist = std::numeric_limits<int>::max();
    for (int i = 0; i < p.colors.size(); ++i) {
        const int d = colorDistance(p.colors[i], c);
        if (d < bestDist) {
            bestDist = d;
            best = i;
            if (d == 0)
                break;
        }
    }
    return best;
}

QString nearestName(const Palette& p, pixel_t c)
{
    const int i = nearest(p, c);
    if (i < 0)
        return QString();
    return colorName(p.colors[i]);
}

} // namespace Palettes
} // namespace pnq
