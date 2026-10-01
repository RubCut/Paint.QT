#pragma once

#include "core/ColorUtils.h"

#include <QColor>
#include <QString>
#include <QVector>

namespace pnq {

struct Palette {
    QString name;
    QVector<pixel_t> colors;

    int size() const { return colors.size(); }
    pixel_t at(int i) const { return (i >= 0 && i < colors.size()) ? colors[i] : 0xFF000000u; }
    int indexOf(pixel_t c) const { return colors.indexOf(c); }
    bool contains(pixel_t c) const { return colors.contains(c); }
    void add(pixel_t c) { colors.append(c); }
};

namespace Palettes {

/// All built-in palettes, in the order they appear in the palette selector.
QVector<Palette> all();
Palette byName(const QString& name);

/// The 216 colour "Web" palette (Paint.NET's default).
Palette web();
Palette grayscale(int count = 256);
Palette nes();
Palette gameBoy();
Palette material(int count = 19);
Palette pastels();
Palette neon();
Palette earthTones();
Palette windows();
Palette autumn();
Palette forest();
Palette ocean();
Palette metallik();

/// Loads a palette file: Paint.NET .pal (text: ";Paint.NET Palette File"), GIMP .gpl,
/// Adobe/hex lists, or a plain text file with one hex colour per line.
bool loadFile(const QString& path, Palette* out, QString* error = nullptr);
bool saveFile(const Palette& pal, const QString& path, QString* error = nullptr);

/// Nearest colour in the palette to the given one.
int nearest(const Palette& p, pixel_t c);
/// Suggested name for a colour inside a palette.
QString nearestName(const Palette& p, pixel_t c);

} // namespace Palettes
} // namespace pnq
