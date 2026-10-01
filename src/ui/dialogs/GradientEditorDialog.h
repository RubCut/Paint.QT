#pragma once

#include "core/Brush.h"
#include "core/Gradient.h"

#include <QDialog>
#include <QVector>

class QSlider;
class QSpinBox;
class QComboBox;
class QCheckBox;
class QLineEdit;
class QPushButton;
class QListWidget;
class QLabel;

namespace pnq {

/// Gradient editor: stop list, position/colour controls, mode and focal point.
class GradientEditorDialog : public QDialog
{
    Q_OBJECT
public:
    explicit GradientEditorDialog(QWidget* parent = nullptr, const Gradient& g = Gradient());

    Gradient gradient() const { return m_gradient; }
    void setGradient(const Gradient& g);

private:
    void rebuildStops();
    void updatePreview();
    void updateButtons();
    void selectStop(int index);

    QListWidget* m_stops = nullptr;
    QSlider* m_position = nullptr;
    QSpinBox* m_positionSpin = nullptr;
    QComboBox* m_mode = nullptr;
    QPushButton* m_colorButton = nullptr;
    QPushButton* m_add = nullptr;
    QPushButton* m_remove = nullptr;
    QLabel* m_preview = nullptr;
    QCheckBox* m_reverse = nullptr;
    Gradient m_gradient;
    int m_current = 0;
    bool m_updating = false;
};

/// Brush editor: shape, size, hardness, spacing, texture preview and custom names.
class BrushEditorDialog : public QDialog
{
    Q_OBJECT
public:
    explicit BrushEditorDialog(QWidget* parent = nullptr, const Brush& b = Brush());

    Brush brush() const { return m_brush; }

private:
    void updatePreview();

    QSlider* m_size = nullptr;
    QSlider* m_hardness = nullptr;
    QSlider* m_opacity = nullptr;
    QSlider* m_spacing = nullptr;
    QComboBox* m_shape = nullptr;
    QComboBox* m_texture = nullptr;
    QCheckBox* m_aa = nullptr;
    QCheckBox* m_erase = nullptr;
    QLabel* m_preview = nullptr;
    QLineEdit* m_name = nullptr;
    Brush m_brush;
    bool m_updating = false;
};

} // namespace pnq
