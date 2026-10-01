#pragma once

#include "ui/EffectsDialog.h"

#include <QWidget>

class QSlider;
class QSpinBox;
class QDoubleSpinBox;
class QComboBox;
class QCheckBox;
class QTabWidget;
class QTableWidget;
class QLabel;

namespace pnq {

/// Brightness/Contrast with live preview and the "use as background" 3D button.
class BrightnessContrastDialog : public QDialog
{
    Q_OBJECT
public:
    BrightnessContrastDialog(QWidget* parent = nullptr, int brightness = 0, int contrast = 0);

    int brightness() const;
    int contrast() const;

signals:
    void previewRequested();

private:
    QSlider* m_brightness = nullptr;
    QSlider* m_contrast = nullptr;
    QLabel* m_bLabel = nullptr;
    QLabel* m_cLabel = nullptr;
};

/// Hue/Saturation/Lightness (optional colorize).
class HueSaturationDialog : public QDialog
{
    Q_OBJECT
public:
    HueSaturationDialog(QWidget* parent = nullptr);
    int hue() const;
    int saturation() const;
    int lightness() const;
    bool colorize() const;

signals:
    void previewRequested();

private:
    QSlider* m_hue = nullptr;
    QSlider* m_sat = nullptr;
    QSlider* m_light = nullptr;
    QCheckBox* m_colorize = nullptr;
    QLabel* m_hLabel = nullptr;
    QLabel* m_sLabel = nullptr;
    QLabel* m_lLabel = nullptr;
};

/// Curves editor (composite + per channel), with a histogram.
class CurvesDialog : public QDialog
{
    Q_OBJECT
public:
    explicit CurvesDialog(QWidget* parent = nullptr);
    /// points: 256 entries, 0..255 mapping.
    QVector<int> points(int channel) const;

signals:
    void previewRequested();

private:
    QTabWidget* m_tabs = nullptr;
    QVector<int> m_rgb, m_r, m_g, m_b;
    QVector<QVector<int>> m_channels;
};

/// Levels with input/output ranges and gamma per channel.
class LevelsDialog : public QDialog
{
    Q_OBJECT
public:
    explicit LevelsDialog(QWidget* parent = nullptr);

    int inputLow() const;
    int inputHigh() const;
    int outputLow() const;
    int outputHigh() const;
    double gamma() const;
    bool usePerChannel() const;
    int channelLow(int c) const;
    int channelHigh(int c) const;
    double channelGamma(int c) const;

signals:
    void previewRequested();

private:
    bool m_perChannel = false;
    struct Ch {
        int inLow = 0, inHigh = 255, outLow = 0, outHigh = 255;
        double gamma = 1.0;
    };
    Ch m_c[4]; ///< rgb, r, g, b
    QCheckBox* m_perChannelCheck = nullptr;

public:
    struct ChannelWidgets {
        QSpinBox* lo = nullptr;
        QSpinBox* hi = nullptr;
        QDoubleSpinBox* gamma = nullptr;

        QSpinBox*& operator[](int i) { return i == 0 ? lo : hi; }
    };
    ChannelWidgets m_channelWidgets[4] = {};
};

/// Selective color with CMYK sliders for 7 colour ranges.
class SelectiveColorDialog : public QDialog
{
    Q_OBJECT
public:
    explicit SelectiveColorDialog(QWidget* parent = nullptr);
    bool relative() const;
    int value(int range) const; ///< 0..6 -> reds..neutrals

signals:
    void previewRequested();

private:
    QSlider* m_sliders[7] = {};
    QSlider* m_cmyk[4] = {};
    QCheckBox* m_relative = nullptr;
};

/// Channel mixer.
class ChannelMixerDialog : public QDialog
{
    Q_OBJECT
public:
    explicit ChannelMixerDialog(QWidget* parent = nullptr);
    int matrix(int dst, int src) const; ///< 0..2 rows/cols
    bool monochrome() const;

signals:
    void previewRequested();

private:
    int m_matrix[3][3] = { { 100, 0, 0 }, { 0, 100, 0 }, { 0, 0, 100 } };
    QCheckBox* m_mono = nullptr;
};

/// Posterize / threshold / desaturate style small dialogs.
class SimpleAdjustmentDialog : public QDialog
{
    Q_OBJECT
public:
    enum class Kind { Posterize, Threshold, Desaturate, ColorBalance, TemperatureTint, ReplaceColor,
                       Invert };
    SimpleAdjustmentDialog(Kind kind, QWidget* parent = nullptr);
    Kind kind() const { return m_kind; }
    QVector<int> values() const { return m_values; }
    bool flag() const { return m_flag; }

signals:
    void previewRequested();

private:
    Kind m_kind;
    QVector<int> m_values;
    bool m_flag = true;
    QVector<QSlider*> m_sliders;
    QCheckBox* m_check = nullptr;
};

} // namespace pnq
