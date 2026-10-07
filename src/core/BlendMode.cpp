#include "core/BlendMode.h"

#include <QHash>

namespace pnq {

namespace {
struct Entry { const char* name; const char* key; };
const Entry kTable[] = {
    { "Normal", "normal" },
    { "Legacy Normal", "legacynormal" },
    { "Dissolve", "dissolve" },
    { "Darken", "darken" },
    { "Multiply", "multiply" },
    { "Color Burn", "colorburn" },
    { "Linear Burn", "linearburn" },
    { "Darker Color", "darkercolor" },
    { "Lighten", "lighten" },
    { "Screen", "screen" },
    { "Color Dodge", "colordodge" },
    { "Linear Dodge (Add)", "lineardodge" },
    { "Lighter Color", "lightercolor" },
    { "Overlay", "overlay" },
    { "Soft Light", "softlight" },
    { "Hard Light", "hardlight" },
    { "Vivid Light", "vividlight" },
    { "Linear Light", "linearlight" },
    { "Pin Light", "pinlight" },
    { "Hard Mix", "hardmix" },
    { "Difference", "difference" },
    { "Exclusion", "exclusion" },
    { "Subtract", "subtract" },
    { "Divide", "divide" },
    { "Hue", "hue" },
    { "Saturation", "saturation" },
    { "Color", "color" },
    { "Luminosity", "luminosity" },
    { "Reflect", "reflect" },
    { "Glow", "glow" },
    { "Negation", "negation" },
    { "XOR", "xor" },
};
static_assert(sizeof(kTable) / sizeof(kTable[0]) == static_cast<size_t>(BlendMode::Count),
              "blend mode table out of sync");
} // namespace

const char* blendModeName(BlendMode m)
{
    int i = static_cast<int>(m);
    if (i < 0 || i >= static_cast<int>(BlendMode::Count))
        i = 0;
    return kTable[i].name;
}

QString blendModeKey(BlendMode m)
{
    int i = static_cast<int>(m);
    if (i < 0 || i >= static_cast<int>(BlendMode::Count))
        i = 0;
    return QString::fromLatin1(kTable[i].key);
}

bool blendModeFromKey(const QString& key, BlendMode* out)
{
    static QHash<QString, BlendMode> map = [] {
        QHash<QString, BlendMode> h;
        for (int i = 0; i < static_cast<int>(BlendMode::Count); ++i)
            h.insert(QString::fromLatin1(kTable[i].key), static_cast<BlendMode>(i));
        return h;
    }();
    auto it = map.constFind(key.toLower());
    if (it == map.constEnd())
        return false;
    if (out)
        *out = it.value();
    return true;
}

QList<BlendMode> allBlendModes()
{
    QList<BlendMode> l;
    for (int i = 0; i < static_cast<int>(BlendMode::Count); ++i)
        l.append(static_cast<BlendMode>(i));
    return l;
}

bool blendModeStartsGroup(BlendMode m)
{
    switch (m) {
    case BlendMode::Normal:
    case BlendMode::Darken:
    case BlendMode::Lighten:
    case BlendMode::Overlay:
    case BlendMode::Difference:
    case BlendMode::Hue:
        return true;
    default:
        return false;
    }
}

} // namespace pnq
