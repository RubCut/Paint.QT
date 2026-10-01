#pragma once

#include "core/ColorUtils.h"
#include "resources/Palettes.h"

#include <QWidget>

class QListWidget;
class QSlider;
class QComboBox;
class QPushButton;
class QGridLayout;

namespace pnq {

class ColorBox;
class ColorWheel;

/// The Colors palette: the paint.NET style dual swatch, palette grid and controls.
class ColorPane : public QWidget
{
    Q_OBJECT
public:
    explicit ColorPane(QWidget* parent = nullptr);

    pixel_t primary() const { return m_primary; }
    pixel_t secondary() const { return m_secondary; }
    void setPrimary(pixel_t c);
    void setSecondary(pixel_t c);
    void swapColors();
    void setDefaultColors();

    void loadUserPalette(const Palette& p);
    void saveUserPalette(const Palette& p);
    Palette userPalette() const;
    bool loadPaletteFile(const QString& path, QString* error = nullptr);
    bool savePaletteFile(const QString& path, QString* error = nullptr);
    void reloadPalettes();

signals:
    void primaryChanged(pixel_t c);
    void secondaryChanged(pixel_t c);
    void colorsChanged(pixel_t p, pixel_t s);
    void statusMessage(const QString& text);

private:
    void buildColorBox();
    void buildPaletteList();
    void rebuildGrid();
    void setColorFromWidget(QWidget* w, bool primary);

    ColorBox* m_box = nullptr;
    ColorWheel* m_wheel = nullptr;
    QSlider* m_value = nullptr;
    QComboBox* m_paletteSelector = nullptr;
    QGridLayout* m_grid = nullptr;
    QPushButton* m_addButton = nullptr;
    QPushButton* m_editButton = nullptr;
    QListWidget* m_recentList = nullptr;

    pixel_t m_primary = 0xFF000000u;
    pixel_t m_secondary = 0xFFFFFFFFu;
    Palette m_current;
    QVector<pixel_t> m_userColors;
    QVector<pixel_t> m_recent;
    bool m_updating = false;
};

} // namespace pnq
