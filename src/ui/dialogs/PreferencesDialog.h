#pragma once

#include <QDialog>
#include <QMap>
#include <QString>

class QComboBox;
class QCheckBox;
class QSpinBox;
class QTabWidget;
class QWidget;

namespace pnq {

class ShortcutDisplayWidget;
/// Application preferences: general, editing, brushes, files, shortcuts, window.
class PreferencesDialog : public QDialog
{
    Q_OBJECT
public:
    explicit PreferencesDialog(QWidget* parent = nullptr);

    /// Current values keyed by setting name (see save()).
    QMap<QString, QVariant> values() const;
    void applyDefaults();
    void save();
    void load();

    QMap<QString, QString> toolShortcuts() const;
    QMap<QString, QString> commandShortcuts() const;
    void setShortcuts(const QMap<QString, QString>& tools, const QMap<QString, QString>& commands);

private:
    QComboBox* m_language = nullptr;
    QComboBox* m_units = nullptr;
    QCheckBox* m_showRulers = nullptr;
    QCheckBox* m_showStatus = nullptr;
    QCheckBox* m_alwaysToolOptions = nullptr;
    QCheckBox* m_snapToGuides = nullptr;
    QSpinBox* m_recentCount = nullptr;
    QSpinBox* m_maxUndo = nullptr;
    QSpinBox* m_defaultLayerOpacity = nullptr;
    QCheckBox* m_antialiasBrushes = nullptr;
    QSpinBox* m_gridSize = nullptr;
    QSpinBox* m_brushSize = nullptr;
    QSpinBox* m_brushHardness = nullptr;
    QSpinBox* m_brushSpacing = nullptr;
    QComboBox* m_brushShape = nullptr;
    QComboBox* m_defaultFormat = nullptr;
    QSpinBox* m_jpegQuality = nullptr;
    QCheckBox* m_warnLossy = nullptr;
    QCheckBox* m_warnTransparency = nullptr;
    QCheckBox* m_warnResize = nullptr;
    QCheckBox* m_warnFlatten = nullptr;
    QCheckBox* m_rememberDir = nullptr;
    QCheckBox* m_saveWindowPos = nullptr;
    QCheckBox* m_saveToolState = nullptr;
    QCheckBox* m_singleInstance = nullptr;
    ShortcutDisplayWidget* m_toolShortcuts = nullptr;
    ShortcutDisplayWidget* m_commandShortcuts = nullptr;
};

} // namespace pnq
