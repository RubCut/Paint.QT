#pragma once

#include "core/Brush.h"

#include <QWidget>

class QListView;
class QComboBox;
class QSlider;
class QLabel;
class QListWidget;
class QCheckBox;
class QStandardItemModel;

namespace pnq {

/// The Brushes palette: preset list, size/hardness/opacity, shape, texture.
class BrushesPane : public QWidget
{
    Q_OBJECT
public:
    explicit BrushesPane(QWidget* parent = nullptr);

    const Brush& currentBrush() const { return m_brush; }
    void setBrush(const Brush& b);
    BrushShape currentShape() const { return m_shape; }
    void setShape(BrushShape s);
    void reloadPresets();
    /// Re-reads the controls when the active tool changes.
    void syncToTool(const QString& toolId);

signals:
    void brushChanged(const Brush& b);
    void shapeChanged(BrushShape s);
    void statusMessage(const QString& text);

private:
    void buildUi();
    void updateControls();
    void emitBrush();
    void emitShape();

    QListView* m_list = nullptr;
    QStandardItemModel* m_model = nullptr;
    QComboBox* m_sizeBox = nullptr;
    QSlider* m_hardness = nullptr;
    QSlider* m_opacity = nullptr;
    QSlider* m_spacing = nullptr;
    QComboBox* m_shapeBox = nullptr;
    QComboBox* m_texture = nullptr;
    QLabel* m_hardnessLabel = nullptr;
    QLabel* m_opacityLabel = nullptr;
    QLabel* m_spacingLabel = nullptr;
    QCheckBox* m_antiAlias = nullptr;
    QLabel* m_preview = nullptr;

    Brush m_brush;
    QList<Brush> m_presets;
    BrushShape m_shape = BrushShape::Round;
    bool m_updating = false;
};

} // namespace pnq
