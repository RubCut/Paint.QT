#pragma once

#include <QWidget>

class QSpinBox;
class QComboBox;
class QCheckBox;
class QSlider;
class QLabel;
class QLineEdit;

namespace pnq {

/// Shape tool parameters: mode, outline width, 3D mode, corner radius, anti-aliasing.
class ShapesPane : public QWidget
{
    Q_OBJECT
public:
    explicit ShapesPane(QWidget* parent = nullptr);

    enum class Mode { Fill = 0, Outline = 1, FillOutline = 2 };

    Mode mode() const { return m_modeVal; }
    int modeIndex() const { return m_modeIndex; }
    int outlineWidth() const { return m_outlineWidth; }
    int outlineOpacity() const { return m_outlineOpacity; }
    bool threeD() const { return m_threeDValue; }
    bool rounded() const { return m_roundedValue; }
    int cornerRadius() const { return m_cornerRadius; }
    bool antiAliasing() const { return m_antiAliasValue; }

signals:
    void parametersChanged();

private:
    QComboBox* m_modeBox = nullptr;
    QSpinBox* m_outlineBox = nullptr;
    QSpinBox* m_cornerBox = nullptr;
    QCheckBox* m_threeDBox = nullptr;
    QCheckBox* m_roundedBox = nullptr;
    QCheckBox* m_antiAliasBox = nullptr;
    QSlider* m_opacitySlider = nullptr;
    Mode m_modeVal = Mode::Fill;
    int m_modeIndex = 0;
    int m_outlineWidth = 1;
    int m_cornerRadius = 8;
    bool m_threeDValue = false;
    bool m_roundedValue = false;
    bool m_antiAliasValue = true;
    int m_outlineOpacity = 255;
};

/// Text tool parameters: font family, size, styles, fill/outline.
class TextPane : public QWidget
{
    Q_OBJECT
public:
    explicit TextPane(QWidget* parent = nullptr);

    QString family() const;
    int fontSize() const;
    bool bold() const;
    bool italic() const;
    bool underline() const;
    bool strikethrough() const;
    bool antiAliasing() const;
    bool filled() const;
    bool outlined() const;
    bool autoFillBackground() const;

signals:
    void parametersChanged();

private:
    QComboBox* m_family = nullptr;
    QSpinBox* m_size = nullptr;
    QCheckBox* m_bold = nullptr;
    QCheckBox* m_italic = nullptr;
    QCheckBox* m_underline = nullptr;
    QCheckBox* m_strike = nullptr;
    QCheckBox* m_aa = nullptr;
    QCheckBox* m_filled = nullptr;
    QCheckBox* m_outlined = nullptr;
    QCheckBox* m_autoFill = nullptr;
};

} // namespace pnq
