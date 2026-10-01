#include "ui/dialogs/PreferencesDialog.h"
#include "ui/dialogs/ShortcutDisplayWidget.h"
#include "core/Brush.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QSettings>
#include <QSpinBox>
#include <QTabWidget>
#include <QVBoxLayout>

namespace pnq {

PreferencesDialog::PreferencesDialog(QWidget* parent) : QDialog(parent)
{
    setWindowTitle(tr("Preferences"));
    setModal(true);
    resize(540, 460);

    QTabWidget* tabs = new QTabWidget(this);

    // --- General ---
    QWidget* general = new QWidget(this);
    QFormLayout* gf = new QFormLayout(general);
    m_language = new QComboBox(general);
    m_language->addItems({ QStringLiteral("English (system)"), QStringLiteral("Deutsch"),
                            QStringLiteral("Español"), QStringLiteral("Français"),
                            QStringLiteral("Русский"), QStringLiteral("日本語"),
                            QStringLiteral("中文（简体）") });
    gf->addRow(tr("Display language:"), m_language);
    m_units = new QComboBox(general);
    m_units->addItems({ tr("Inches"), tr("Centimeters"), tr("Pixels") });
    gf->addRow(tr("Units of measurement:"), m_units);
    m_showRulers = new QCheckBox(tr("Show rulers"), general);
    gf->addRow(m_showRulers);
    m_showStatus = new QCheckBox(tr("Show status bar"), general);
    gf->addRow(m_showStatus);
    m_alwaysToolOptions = new QCheckBox(tr("Always show tool options"), general);
    gf->addRow(m_alwaysToolOptions);
    m_snapToGuides = new QCheckBox(tr("Snap to guides and grid"), general);
    gf->addRow(m_snapToGuides);
    m_recentCount = new QSpinBox(general);
    m_recentCount->setRange(1, 20);
    gf->addRow(tr("Recent files in menu:"), m_recentCount);
    tabs->addTab(general, tr("General"));

    // --- Editing ---
    QWidget* editing = new QWidget(this);
    QFormLayout* ef = new QFormLayout(editing);
    m_maxUndo = new QSpinBox(editing);
    m_maxUndo->setRange(3, 1024);
    m_maxUndo->setToolTip(tr("Maximum number of undo levels kept in memory"));
    ef->addRow(tr("Maximum undo levels:"), m_maxUndo);
    m_defaultLayerOpacity = new QSpinBox(editing);
    m_defaultLayerOpacity->setRange(0, 100);
    m_defaultLayerOpacity->setSuffix(QStringLiteral(" %"));
    ef->addRow(tr("Default layer opacity:"), m_defaultLayerOpacity);
    m_antialiasBrushes = new QCheckBox(tr("Antialias brush strokes"), editing);
    ef->addRow(m_antialiasBrushes);
    m_gridSize = new QSpinBox(editing);
    m_gridSize->setRange(2, 512);
    m_gridSize->setSuffix(tr(" px"));
    ef->addRow(tr("Grid size:"), m_gridSize);
    tabs->addTab(editing, tr("Editing"));

    // --- Brushes ---
    QWidget* brushes = new QWidget(this);
    QFormLayout* bf = new QFormLayout(brushes);
    m_brushSize = new QSpinBox(brushes);
    m_brushSize->setRange(1, 500);
    bf->addRow(tr("Default brush size:"), m_brushSize);
    m_brushHardness = new QSpinBox(brushes);
    m_brushHardness->setRange(0, 100);
    bf->addRow(tr("Default brush hardness:"), m_brushHardness);
    m_brushSpacing = new QSpinBox(brushes);
    m_brushSpacing->setRange(1, 100);
    bf->addRow(tr("Default brush spacing:"), m_brushSpacing);
    m_brushShape = new QComboBox(brushes);
    for (int i = 0; i < int(BrushShape::Count); ++i)
        m_brushShape->addItem(brushShapeName(BrushShape(i)), i);
    bf->addRow(tr("Default brush shape:"), m_brushShape);
    tabs->addTab(brushes, tr("Brushes"));

    // --- Files ---
    QWidget* files = new QWidget(this);
    QFormLayout* ff = new QFormLayout(files);
    m_defaultFormat = new QComboBox(files);
    m_defaultFormat->addItems({ QStringLiteral("png"), QStringLiteral("jpg"), QStringLiteral("bmp"),
                                QStringLiteral("tif"), QStringLiteral("webp") });
    ff->addRow(tr("Default save format:"), m_defaultFormat);
    m_jpegQuality = new QSpinBox(files);
    m_jpegQuality->setRange(1, 100);
    ff->addRow(tr("Default JPEG quality:"), m_jpegQuality);
    m_warnLossy = new QCheckBox(tr("Warn when saving to a lossy format"), files);
    ff->addRow(m_warnLossy);
    m_warnTransparency = new QCheckBox(tr("Warn when saving without transparency"), files);
    ff->addRow(m_warnTransparency);
    m_warnResize = new QCheckBox(tr("Warn when resizing the image"), files);
    ff->addRow(m_warnResize);
    m_warnFlatten = new QCheckBox(tr("Warn when flattening the image"), files);
    ff->addRow(m_warnFlatten);
    m_rememberDir = new QCheckBox(tr("Remember the last used directory"), files);
    ff->addRow(m_rememberDir);
    tabs->addTab(files, tr("Files"));

    // --- Shortcuts ---
    QWidget* shortcuts = new QWidget(this);
    QVBoxLayout* sf = new QVBoxLayout(shortcuts);
    m_toolShortcuts = new ShortcutDisplayWidget(shortcuts, ShortcutDisplayWidget::Mode::Tools);
    m_commandShortcuts = new ShortcutDisplayWidget(shortcuts, ShortcutDisplayWidget::Mode::Commands);
    QTabWidget* st = new QTabWidget(shortcuts);
    st->addTab(m_toolShortcuts, tr("Tools"));
    st->addTab(m_commandShortcuts, tr("Commands"));
    sf->addWidget(st);
    tabs->addTab(shortcuts, tr("Shortcuts"));

    // --- Window ---
    QWidget* window = new QWidget(this);
    QFormLayout* wf = new QFormLayout(window);
    m_saveWindowPos = new QCheckBox(tr("Save window position and size"), window);
    wf->addRow(m_saveWindowPos);
    m_saveToolState = new QCheckBox(tr("Save tool selection and options"), window);
    wf->addRow(m_saveToolState);
    m_singleInstance = new QCheckBox(tr("Open files in a single window"), window);
    wf->addRow(m_singleInstance);
    tabs->addTab(window, tr("Window"));

    QVBoxLayout* l = new QVBoxLayout(this);
    l->addWidget(tabs);

    QDialogButtonBox* bb = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::RestoreDefaults, this);
    connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(bb, &QDialogButtonBox::clicked, this, [this, bb] {
        if (bb->button(QDialogButtonBox::RestoreDefaults))
            applyDefaults();
    });
    l->addWidget(bb);
    load();
}

void PreferencesDialog::applyDefaults()
{
    m_showRulers->setChecked(true);
    m_showStatus->setChecked(true);
    m_alwaysToolOptions->setChecked(true);
    m_snapToGuides->setChecked(true);
    m_recentCount->setValue(8);
    m_maxUndo->setValue(20);
    m_defaultLayerOpacity->setValue(100);
    m_antialiasBrushes->setChecked(true);
    m_gridSize->setValue(32);
    m_brushSize->setValue(19);
    m_brushHardness->setValue(60);
    m_brushSpacing->setValue(20);
    m_brushShape->setCurrentIndex(0);
    m_defaultFormat->setCurrentText(QStringLiteral("png"));
    m_jpegQuality->setValue(90);
    m_warnLossy->setChecked(true);
    m_warnTransparency->setChecked(true);
    m_warnResize->setChecked(true);
    m_warnFlatten->setChecked(true);
    m_rememberDir->setChecked(true);
    m_saveWindowPos->setChecked(true);
    m_saveToolState->setChecked(true);
    m_singleInstance->setChecked(false);
}

void PreferencesDialog::load()
{
    QSettings s;
    m_language->setCurrentIndex(s.value(QStringLiteral("prefs/language"), 0).toInt());
    m_units->setCurrentIndex(s.value(QStringLiteral("prefs/units"), 0).toInt());
    m_showRulers->setChecked(s.value(QStringLiteral("ui/rulers"), true).toBool());
    m_showStatus->setChecked(s.value(QStringLiteral("ui/statusbar"), true).toBool());
    m_alwaysToolOptions->setChecked(s.value(QStringLiteral("ui/alwaysToolOptions"), true).toBool());
    m_snapToGuides->setChecked(s.value(QStringLiteral("canvas/snap"), true).toBool());
    m_recentCount->setValue(s.value(QStringLiteral("recent/max"), 8).toInt());
    m_maxUndo->setValue(s.value(QStringLiteral("undo/maxLength"), 20).toInt());
    m_defaultLayerOpacity->setValue(s.value(QStringLiteral("layers/defaultOpacity"), 100).toInt());
    m_antialiasBrushes->setChecked(s.value(QStringLiteral("brush/aa"), true).toBool());
    m_gridSize->setValue(s.value(QStringLiteral("canvas/gridSize"), 32).toInt());
    m_brushSize->setValue(s.value(QStringLiteral("brush/size"), 19).toInt());
    m_brushHardness->setValue(s.value(QStringLiteral("brush/hardness"), 60).toInt());
    m_brushSpacing->setValue(s.value(QStringLiteral("brush/spacing"), 20).toInt());
    m_brushShape->setCurrentIndex(s.value(QStringLiteral("brush/shape"), 0).toInt());
    m_defaultFormat->setCurrentText(
        s.value(QStringLiteral("files/defaultFormat"), QStringLiteral("png")).toString());
    m_jpegQuality->setValue(s.value(QStringLiteral("files/jpegQuality"), 90).toInt());
    m_warnLossy->setChecked(s.value(QStringLiteral("files/warnLossy"), true).toBool());
    m_warnTransparency->setChecked(s.value(QStringLiteral("files/warnTransparency"), true).toBool());
    m_warnResize->setChecked(s.value(QStringLiteral("files/warnResize"), true).toBool());
    m_warnFlatten->setChecked(s.value(QStringLiteral("files/warnFlatten"), true).toBool());
    m_rememberDir->setChecked(s.value(QStringLiteral("files/rememberDir"), true).toBool());
    m_saveWindowPos->setChecked(s.value(QStringLiteral("window/savePos"), true).toBool());
    m_saveToolState->setChecked(s.value(QStringLiteral("window/saveToolState"), true).toBool());
    m_singleInstance->setChecked(s.value(QStringLiteral("window/singleInstance"), false).toBool());
}

void PreferencesDialog::save()
{
    QSettings s;
    s.setValue(QStringLiteral("prefs/language"), m_language->currentIndex());
    s.setValue(QStringLiteral("prefs/units"), m_units->currentIndex());
    s.setValue(QStringLiteral("ui/rulers"), m_showRulers->isChecked());
    s.setValue(QStringLiteral("ui/statusbar"), m_showStatus->isChecked());
    s.setValue(QStringLiteral("ui/alwaysToolOptions"), m_alwaysToolOptions->isChecked());
    s.setValue(QStringLiteral("canvas/snap"), m_snapToGuides->isChecked());
    s.setValue(QStringLiteral("recent/max"), m_recentCount->value());
    s.setValue(QStringLiteral("undo/maxLength"), m_maxUndo->value());
    s.setValue(QStringLiteral("layers/defaultOpacity"), m_defaultLayerOpacity->value());
    s.setValue(QStringLiteral("brush/aa"), m_antialiasBrushes->isChecked());
    s.setValue(QStringLiteral("canvas/gridSize"), m_gridSize->value());
    s.setValue(QStringLiteral("brush/size"), m_brushSize->value());
    s.setValue(QStringLiteral("brush/hardness"), m_brushHardness->value());
    s.setValue(QStringLiteral("brush/spacing"), m_brushSpacing->value());
    s.setValue(QStringLiteral("brush/shape"), m_brushShape->currentIndex());
    s.setValue(QStringLiteral("files/defaultFormat"), m_defaultFormat->currentText());
    s.setValue(QStringLiteral("files/jpegQuality"), m_jpegQuality->value());
    s.setValue(QStringLiteral("files/warnLossy"), m_warnLossy->isChecked());
    s.setValue(QStringLiteral("files/warnTransparency"), m_warnTransparency->isChecked());
    s.setValue(QStringLiteral("files/warnResize"), m_warnResize->isChecked());
    s.setValue(QStringLiteral("files/warnFlatten"), m_warnFlatten->isChecked());
    s.setValue(QStringLiteral("files/rememberDir"), m_rememberDir->isChecked());
    s.setValue(QStringLiteral("window/savePos"), m_saveWindowPos->isChecked());
    s.setValue(QStringLiteral("window/saveToolState"), m_saveToolState->isChecked());
    s.setValue(QStringLiteral("window/singleInstance"), m_singleInstance->isChecked());
}

QMap<QString, QVariant> PreferencesDialog::values() const
{
    return {
        { QStringLiteral("undo/maxLength"), m_maxUndo->value() },
        { QStringLiteral("ui/rulers"), m_showRulers->isChecked() },
        { QStringLiteral("ui/statusbar"), m_showStatus->isChecked() },
        { QStringLiteral("ui/alwaysToolOptions"), m_alwaysToolOptions->isChecked() },
        { QStringLiteral("canvas/snap"), m_snapToGuides->isChecked() },
        { QStringLiteral("canvas/gridSize"), m_gridSize->value() },
        { QStringLiteral("brush/size"), m_brushSize->value() },
        { QStringLiteral("brush/hardness"), m_brushHardness->value() },
        { QStringLiteral("brush/spacing"), m_brushSpacing->value() },
        { QStringLiteral("brush/aa"), m_antialiasBrushes->isChecked() },
        { QStringLiteral("files/defaultFormat"), m_defaultFormat->currentText() },
        { QStringLiteral("files/jpegQuality"), m_jpegQuality->value() },
        { QStringLiteral("files/warnLossy"), m_warnLossy->isChecked() },
        { QStringLiteral("files/warnTransparency"), m_warnTransparency->isChecked() },
        { QStringLiteral("files/warnResize"), m_warnResize->isChecked() },
        { QStringLiteral("files/warnFlatten"), m_warnFlatten->isChecked() },
        { QStringLiteral("files/rememberDir"), m_rememberDir->isChecked() },
        { QStringLiteral("window/savePos"), m_saveWindowPos->isChecked() },
        { QStringLiteral("window/singleInstance"), m_singleInstance->isChecked() },
    };
}

QMap<QString, QString> PreferencesDialog::toolShortcuts() const
{
    return m_toolShortcuts ? m_toolShortcuts->shortcuts() : QMap<QString, QString>();
}

QMap<QString, QString> PreferencesDialog::commandShortcuts() const
{
    return m_commandShortcuts ? m_commandShortcuts->shortcuts() : QMap<QString, QString>();
}

void PreferencesDialog::setShortcuts(const QMap<QString, QString>& tools,
                                     const QMap<QString, QString>& commands)
{
    if (m_toolShortcuts)
        m_toolShortcuts->setShortcuts(tools);
    if (m_commandShortcuts)
        m_commandShortcuts->setShortcuts(commands);
}

} // namespace pnq
