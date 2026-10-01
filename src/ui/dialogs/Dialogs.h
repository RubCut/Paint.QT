#pragma once

#include "core/Brush.h"
#include "core/ColorUtils.h"
#include "core/Gradient.h"
#include "core/Layer.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDoubleSpinBox>
#include <QGridLayout>
#include <QLabel>
#include <QMap>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QVBoxLayout>

class QLineEdit;
class QGroupBox;

namespace pnq {

class SatValBox;

/// HSV colour picker with the Paint.NET style layout (plane + sliders + hex).
class ColorPickerDialog : public QDialog
{
    Q_OBJECT
public:
    ColorPickerDialog(QWidget* parent, const QString& title, const QColor& initial, bool withAlpha);

    QColor selectedColor() const;
    void setSelectedColor(const QColor& c);
    pixel_t selectedPixel() const { return toPixel(selectedColor()); }
    /// Returns an invalid QColor when the user cancels.
    static QColor pickColor(QWidget* parent, const QString& title, const QColor& initial,
                            bool withAlpha = true);

private:
    void buildUi();
    void updateFromSliders();
    void updateFromSpin();
    void refreshFields();
    QColor hsvColor() const { return m_color; }

    SatValBox* m_satVal = nullptr;
    QWidget* m_hueBand = nullptr;
    QSlider* m_hue = nullptr;
    QSlider* m_sat = nullptr;
    QSlider* m_val = nullptr;
    QSpinBox* m_alpha = nullptr;
    QWidget* m_alphaRow = nullptr;
    QSpinBox* m_r = nullptr;
    QSpinBox* m_g = nullptr;
    QSpinBox* m_b = nullptr;
    QSpinBox* m_hex = nullptr;
    QLabel* m_preview = nullptr;
    QLabel* m_nameLabel = nullptr;
    QCheckBox* m_withAlpha = nullptr;
    QColor m_color = Qt::white;
    bool m_updating = false;
    bool m_hasAlpha = true;
};

// ---------------------------------------------------------------- new image

class NewImageDialog : public QDialog
{
    Q_OBJECT
public:
    explicit NewImageDialog(QWidget* parent = nullptr);
    int imageWidth() const;
    int imageHeight() const;
    bool singleLayerBackground() const;
    QColor backgroundColor() const;
    void setWidth(int w);
    void setHeight(int h);

private:
    QComboBox* m_width = nullptr;
    QComboBox* m_height = nullptr;
    QComboBox* m_units = nullptr;
    QCheckBox* m_singleLayer = nullptr;
    QCheckBox* m_transparent = nullptr;
    QPushButton* m_colorButton = nullptr;
    QColor m_color = Qt::white;
    bool m_lockAspect = true;
    bool m_updating = false;
};

// ---------------------------------------------------------------- resize

class ResizeDialog : public QDialog
{
    Q_OBJECT
public:
    ResizeDialog(QWidget* parent, int currentWidth, int currentHeight, bool absolute);
    int width();
    int height();
    bool maintainAspect() const;
    bool smooth() const;
    bool percentageMode() const { return m_percentage; }
    double percentX();
    double percentY();
    void setValues(int w, int h);
    /// Index of the selected resize mode in the combo box.
    int modeIndex() const;

private:
    void updateAspect(int changedIndex);
    QComboBox* m_w = nullptr;
    QComboBox* m_h = nullptr;
    QCheckBox* m_aspect = nullptr;
    QCheckBox* m_smooth = nullptr;
    QComboBox* m_mode = nullptr;
    bool m_percentage = false;
    double m_percentW = 100.0, m_percentH = 100.0;
    int m_currentW = 0, m_currentH = 0;
    bool m_updating = false;
};

// ---------------------------------------------------------------- canvas size

class CanvasSizeDialog : public QDialog
{
    Q_OBJECT
public:
    CanvasSizeDialog(QWidget* parent, int w, int h);
    int width() const;
    int height() const;
    Qt::Alignment anchor() const;

private:
    QComboBox* m_w = nullptr;
    QComboBox* m_h = nullptr;
    QComboBox* m_anchor = nullptr;
    bool m_updating = false;
};

// ---------------------------------------------------------------- layer props

class LayerPropertiesDialog : public QDialog
{
    Q_OBJECT
public:
    LayerPropertiesDialog(QWidget* parent, const Layer& layer);
    QString layerName() const;
    bool visible() const;
    int opacity() const;
    BlendMode blendMode() const;
    LayerLock lockMode() const;
    bool isBackground() const;

private:
    QLineEdit* m_name = nullptr;
    QCheckBox* m_visible = nullptr;
    QSlider* m_opacity = nullptr;
    QLabel* m_opacityLabel = nullptr;
    QComboBox* m_mode = nullptr;
    QComboBox* m_lock = nullptr;
    QCheckBox* m_background = nullptr;
};

} // namespace pnq
