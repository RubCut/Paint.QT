#pragma once

#include <QString>
#include <QStringList>

namespace pnq {

/// Blend modes, matching the set offered by Paint.NET.
enum class BlendMode {
    Normal = 0,
    LegacyNormal,
    Dissolve,
    Darken,
    Multiply,
    ColorBurn,
    LinearBurn,
    DarkerColor,
    Lighten,
    Screen,
    ColorDodge,
    LinearDodge,
    LighterColor,
    Overlay,
    SoftLight,
    HardLight,
    VividLight,
    LinearLight,
    PinLight,
    HardMix,
    Difference,
    Exclusion,
    Subtract,
    Divide,
    Hue,
    Saturation,
    Color,
    Luminosity,
    Count
};

/// Human readable (translatable at the UI layer) name.
const char* blendModeName(BlendMode m);
/// Name in lower case, used for the project file.
QString blendModeKey(BlendMode m);
bool blendModeFromKey(const QString& key, BlendMode* out);
/// All modes in display order.
QList<BlendMode> allBlendModes();
/// Group separators inside the combo box (after these modes).
bool blendModeStartsGroup(BlendMode m);

} // namespace pnq
