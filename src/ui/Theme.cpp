#include "ui/Theme.h"
#include "resources/Icons.h"

#include <QApplication>
#include <QCoreApplication>
#include <QEvent>
#include <QScopeGuard>
#include <QGuiApplication>
#include <QHash>
#include <QPalette>
#include <QSettings>
#include <QStyle>
#include <QStyleFactory>
#include <QStyleHints>
#include <Qt>

#include <mutex>

namespace pnq {

namespace {

// One colour table per variant. The stylesheet below refers to these by name, so
// the light and the dark look stay in step automatically.
QHash<QString, QString> lightColours()
{
    return {
        { QStringLiteral("@window@"), QStringLiteral("#f0f0f0") },
        { QStringLiteral("@windowText@"), QStringLiteral("#1a1a1a") },
        { QStringLiteral("@base@"), QStringLiteral("#ffffff") },
        { QStringLiteral("@altBase@"), QStringLiteral("#f7f7f7") },
        { QStringLiteral("@text@"), QStringLiteral("#1a1a1a") },
        { QStringLiteral("@button@"), QStringLiteral("#f0f0f0") },
        { QStringLiteral("@buttonText@"), QStringLiteral("#1a1a1a") },
        { QStringLiteral("@border@"), QStringLiteral("#adadad") },
        { QStringLiteral("@borderSoft@"), QStringLiteral("#dcdcdc") },
        { QStringLiteral("@borderFaint@"), QStringLiteral("#e0e0e0") },
        { QStringLiteral("@disabled@"), QStringLiteral("#a0a0a0") },
        { QStringLiteral("@disabledBg@"), QStringLiteral("#f4f4f4") },
        { QStringLiteral("@hover@"), QStringLiteral("#e8f1fb") },
        { QStringLiteral("@hoverBorder@"), QStringLiteral("#7da7d9") },
        { QStringLiteral("@pressed@"), QStringLiteral("#d0e0f2") },
        { QStringLiteral("@check@"), QStringLiteral("#cfe2f7") },
        { QStringLiteral("@checkBorder@"), QStringLiteral("#7da7d9") },
        { QStringLiteral("@accent@"), QStringLiteral("#5b8ac4") },
        { QStringLiteral("@accentDeep@"), QStringLiteral("#3a6ea5") },
        { QStringLiteral("@accentSoft@"), QStringLiteral("#6f9ed8") },
        { QStringLiteral("@sliderTrack@"), QStringLiteral("#e0e0e0") },
        { QStringLiteral("@sliderTrackEdge@"), QStringLiteral("#c4c4c4") },
        { QStringLiteral("@sliderHandle@"), QStringLiteral("#f6f6f6") },
        { QStringLiteral("@scrollHandle@"), QStringLiteral("#cdcdcd") },
        { QStringLiteral("@titleTop@"), QStringLiteral("#fdfdfd") },
        { QStringLiteral("@titleMid@"), QStringLiteral("#eaeaea") },
        { QStringLiteral("@titleBottom@"), QStringLiteral("#dcdcdc") },
        { QStringLiteral("@menuBg@"), QStringLiteral("#f7f7f7") },
        { QStringLiteral("@menuBorder@"), QStringLiteral("#a0a0a0") },
        { QStringLiteral("@sep@"), QStringLiteral("#d0d0d0") },
        { QStringLiteral("@tabBg@"), QStringLiteral("#e8e8e8") },
        { QStringLiteral("@tabSelBg@"), QStringLiteral("#f7f7f7") },
        { QStringLiteral("@tabBorder@"), QStringLiteral("#b4b4b4") },
        { QStringLiteral("@dockBorder@"), QStringLiteral("#b4b4b4") },
        { QStringLiteral("@grip@"), QStringLiteral("#a8a8a8") },
        { QStringLiteral("@hint@"), QStringLiteral("#5c5c5c") },
        { QStringLiteral("@link@"), QStringLiteral("#1a5fa8") },
        { QStringLiteral("@placeholder@"), QStringLiteral("#8a8a8a") },
        { QStringLiteral("@tooltip@"), QStringLiteral("#ffffe1") },
        { QStringLiteral("@shadow@"), QStringLiteral("rgba(0,0,0,28)") },
    };
}

QHash<QString, QString> darkColours()
{
    return {
        { QStringLiteral("@window@"), QStringLiteral("#2b2b2b") },
        { QStringLiteral("@windowText@"), QStringLiteral("#e6e6e6") },
        { QStringLiteral("@base@"), QStringLiteral("#1e1e1e") },
        { QStringLiteral("@altBase@"), QStringLiteral("#262626") },
        { QStringLiteral("@text@"), QStringLiteral("#e6e6e6") },
        { QStringLiteral("@button@"), QStringLiteral("#333333") },
        { QStringLiteral("@buttonText@"), QStringLiteral("#e6e6e6") },
        { QStringLiteral("@border@"), QStringLiteral("#4a4a4a") },
        { QStringLiteral("@borderSoft@"), QStringLiteral("#3a3a3a") },
        { QStringLiteral("@borderFaint@"), QStringLiteral("#3f3f3f") },
        { QStringLiteral("@disabled@"), QStringLiteral("#6f6f6f") },
        { QStringLiteral("@disabledBg@"), QStringLiteral("#2f2f2f") },
        { QStringLiteral("@hover@"), QStringLiteral("#333c46") },
        { QStringLiteral("@hoverBorder@"), QStringLiteral("#5b7fae") },
        { QStringLiteral("@pressed@"), QStringLiteral("#2f3944") },
        { QStringLiteral("@check@"), QStringLiteral("#33475e") },
        { QStringLiteral("@checkBorder@"), QStringLiteral("#5b7fae") },
        { QStringLiteral("@accent@"), QStringLiteral("#4a7fbf") },
        { QStringLiteral("@accentDeep@"), QStringLiteral("#6a9ad4") },
        { QStringLiteral("@accentSoft@"), QStringLiteral("#4a7fbf") },
        { QStringLiteral("@sliderTrack@"), QStringLiteral("#3a3a3a") },
        { QStringLiteral("@sliderTrackEdge@"), QStringLiteral("#4a4a4a") },
        { QStringLiteral("@sliderHandle@"), QStringLiteral("#d8d8d8") },
        { QStringLiteral("@scrollHandle@"), QStringLiteral("#4a4a4a") },
        { QStringLiteral("@titleTop@"), QStringLiteral("#3d3d3d") },
        { QStringLiteral("@titleMid@"), QStringLiteral("#353535") },
        { QStringLiteral("@titleBottom@"), QStringLiteral("#2e2e2e") },
        { QStringLiteral("@menuBg@"), QStringLiteral("#303030") },
        { QStringLiteral("@menuBorder@"), QStringLiteral("#4a4a4a") },
        { QStringLiteral("@sep@"), QStringLiteral("#454545") },
        { QStringLiteral("@tabBg@"), QStringLiteral("#313131") },
        { QStringLiteral("@tabSelBg@"), QStringLiteral("#3a3a3a") },
        { QStringLiteral("@tabBorder@"), QStringLiteral("#4a4a4a") },
        { QStringLiteral("@dockBorder@"), QStringLiteral("#4a4a4a") },
        { QStringLiteral("@grip@"), QStringLiteral("#6a6a6a") },
        { QStringLiteral("@hint@"), QStringLiteral("#a8a8a8") },
        { QStringLiteral("@link@"), QStringLiteral("#6cb0f0") },
        { QStringLiteral("@placeholder@"), QStringLiteral("#7a7a7a") },
        { QStringLiteral("@tooltip@"), QStringLiteral("#3a3a3a") },
        { QStringLiteral("@shadow@"), QStringLiteral("rgba(0,0,0,90)") },
    };
}

QString styleSheetTemplate()
{
    return QStringLiteral(R"CSS(
/* ---- base ------------------------------------------------------------- */
QWidget {
    background-color: @window@;
    color: @windowText@;
    font-family: "Segoe UI", "Noto Sans", "DejaVu Sans", sans-serif;
    font-size: 9pt;
}
QMainWindow, QDialog { background-color: @window@; }
QWidget:disabled { color: @disabled@; }
QLabel { background: transparent; }

/* ---- menu bar --------------------------------------------------------- */
QMenuBar { background-color: @window@; border: none; padding: 1px; }
QMenuBar::item {
    background: transparent;
    padding: 3px 8px;
    border: 1px solid transparent;
}
QMenuBar::item:selected { background-color: @check@; border: 1px solid @checkBorder@; }
QMenu {
    background-color: @menuBg@;
    border: 1px solid @menuBorder@;
    padding: 2px;
}
QMenu::item { padding: 4px 26px 4px 22px; border: 1px solid transparent; }
QMenu::item:selected { background-color: @check@; border: 1px solid @checkBorder@; }
QMenu::item:disabled { color: @disabled@; }
QMenu::separator { height: 1px; background: @sep@; margin: 3px 6px; }
QMenu::icon { padding-left: 4px; }

/* ---- tool bars -------------------------------------------------------- */
QToolBar { background-color: @window@; border: none; spacing: 1px; padding: 1px; }
QToolBar::separator { background: @sep@; width: 1px; margin: 3px; }
QToolButton {
    background: transparent;
    border: 1px solid transparent;
    border-radius: 2px;
    padding: 2px;
}
QToolButton:hover { background-color: @hover@; border: 1px solid @hoverBorder@; }
QToolButton:pressed { background-color: @pressed@; border: 1px solid @checkBorder@; }
QToolButton:checked { background-color: @check@; border: 1px solid @checkBorder@; }
QToolBar#toolsToolBar {
    background-color: @window@;
    border-right: 1px solid @borderSoft@;
    padding: 2px;
}
QToolBar#toolsToolBar QToolButton { width: 28px; height: 28px; padding: 2px; }

/* ---- push buttons ----------------------------------------------------- */
QPushButton {
    background-color: @button@;
    border: 1px solid @border@;
    border-radius: 2px;
    padding: 3px 10px;
    min-height: 17px;
}
QPushButton:hover { background-color: @hover@; border-color: @hoverBorder@; }
QPushButton:pressed { background-color: @pressed@; border-color: @accentDeep@; }
QPushButton:disabled { background-color: @disabledBg@; color: @disabled@; border-color: @borderSoft@; }
QPushButton:default { border: 1px solid @accentDeep@; }

/* ---- tool tips -------------------------------------------------------- */
QToolTip {
    background-color: @tooltip@;
    color: @windowText@;
    border: 1px solid @border@;
    padding: 3px 5px;
}

/* ---- sliders ---------------------------------------------------------- */
QSlider::groove:horizontal {
    height: 4px;
    background: @sliderTrack@;
    border: 1px solid @sliderTrackEdge@;
    border-radius: 2px;
}
QSlider::sub-page:horizontal { background: @accentSoft@; border-radius: 2px; }
QSlider::add-page:horizontal { background: @sliderTrack@; border-radius: 2px; }
QSlider::handle:horizontal {
    background: @sliderHandle@;
    border: 1px solid @border@;
    width: 9px;
    margin: -5px 0;
    border-radius: 3px;
}
QSlider::handle:horizontal:hover { background: @hover@; border-color: @hoverBorder@; }
QSlider::groove:vertical {
    width: 4px;
    background: @sliderTrack@;
    border: 1px solid @sliderTrackEdge@;
    border-radius: 2px;
}
QSlider::add-page:vertical { background: @accentSoft@; border-radius: 2px; }
QSlider::handle:vertical {
    background: @sliderHandle@;
    border: 1px solid @border@;
    height: 9px;
    margin: 0 -5px;
    border-radius: 3px;
}

/* ---- spin boxes and combos -------------------------------------------- */
QSpinBox, QDoubleSpinBox {
    background-color: @base@;
    border: 1px solid @border@;
    border-radius: 2px;
    padding: 1px 2px;
    min-height: 17px;
    selection-background-color: @check@;
    selection-color: @text@;
}
QSpinBox:focus, QDoubleSpinBox:focus { border-color: @accentDeep@; }
QSpinBox:disabled, QDoubleSpinBox:disabled { background-color: @disabledBg@; color: @disabled@; }
QComboBox {
    background-color: @base@;
    border: 1px solid @border@;
    border-radius: 2px;
    padding: 1px 18px 1px 4px;
    min-height: 17px;
}
QComboBox:hover { border-color: @hoverBorder@; }
QComboBox:focus { border-color: @accentDeep@; }
QComboBox::drop-down {
    subcontrol-origin: padding;
    subcontrol-position: center right;
    width: 15px;
    border-left: 1px solid @borderSoft@;
}
QComboBox::down-arrow {
    image: none;
    border-left: 4px solid transparent;
    border-right: 4px solid transparent;
    border-top: 5px solid @windowText@;
    width: 0;
    height: 0;
    margin-right: 5px;
}
QComboBox QAbstractItemView {
    background-color: @base@;
    border: 1px solid @menuBorder@;
    selection-background-color: @check@;
    selection-color: @text@;
    outline: none;
}

/* ---- check boxes and radios ------------------------------------------- */
QCheckBox, QRadioButton { background: transparent; spacing: 5px; padding: 1px; }
QCheckBox::indicator, QRadioButton::indicator { width: 13px; height: 13px; }
QCheckBox::indicator { background: @base@; border: 1px solid @border@; border-radius: 2px; }
QCheckBox::indicator:hover { border-color: @accentDeep@; }
QCheckBox::indicator:checked { background: @accent@; border: 1px solid @accentDeep@; }
QRadioButton::indicator { background: @base@; border: 1px solid @border@; border-radius: 8px; }
QRadioButton::indicator:checked { background: @accent@; border: 2px solid @base@; outline: 1px solid @accentDeep@; }

/* ---- lists, trees and tables (the palettes) --------------------------- */
QListWidget, QTreeWidget, QTableWidget, QListView, QTreeView {
    background-color: @base@;
    border: 1px solid @border@;
    alternate-background-color: @altBase@;
    outline: none;
    selection-background-color: @check@;
    selection-color: @text@;
}
QListWidget::item, QTreeWidget::item { padding: 1px; border: 1px solid transparent; }
QListWidget::item:selected, QTreeWidget::item:selected { background-color: @check@; color: @text@; }
QListWidget::item:hover, QTreeWidget::item:hover { background-color: @hover@; }
QHeaderView::section {
    background-color: @window@;
    border: none;
    border-right: 1px solid @borderSoft@;
    border-bottom: 1px solid @border@;
    padding: 2px 4px;
}
QTableView { gridline-color: @borderFaint@; }

/* ---- scroll bars ------------------------------------------------------ */
QScrollBar:vertical {
    background: @window@;
    width: 13px;
    margin: 0;
    border-left: 1px solid @borderFaint@;
}
QScrollBar::handle:vertical {
    background: @scrollHandle@;
    border: 1px solid @border@;
    border-radius: 5px;
    min-height: 24px;
    margin: 1px 2px;
}
QScrollBar::handle:vertical:hover { background: @hoverBorder@; }
QScrollBar:horizontal {
    background: @window@;
    height: 13px;
    margin: 0;
    border-top: 1px solid @borderFaint@;
}
QScrollBar::handle:horizontal {
    background: @scrollHandle@;
    border: 1px solid @border@;
    border-radius: 5px;
    min-width: 24px;
    margin: 2px 1px;
}
QScrollBar::handle:horizontal:hover { background: @hoverBorder@; }
QScrollBar::add-line, QScrollBar::sub-line { height: 0; width: 0; }
QScrollBar::add-page, QScrollBar::sub-page { background: none; }
QScrollArea { background: @window@; border: none; }

/* ---- dock widgets ----------------------------------------------------- */
QDockWidget {
    titlebar-close-icon: none;
    titlebar-normal-icon: none;
    font-weight: bold;
}
QDockWidget::title {
    background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
                stop:0 @titleTop@, stop:0.5 @titleMid@, stop:1 @titleBottom@);
    border: 1px solid @dockBorder@;
    border-bottom: none;
    border-top-left-radius: 3px;
    border-top-right-radius: 3px;
    padding: 3px 6px;
    text-align: left;
}
QDockWidget::close-button, QDockWidget::float-button {
    background: transparent;
    border: none;
    padding: 0;
    subcontrol-position: center;
    subcontrol-origin: padding;
}
QDockWidget > QWidget { background-color: @window@; border: 1px solid @dockBorder@; }

/* ---- tabs ------------------------------------------------------------- */
QTabBar::tab {
    background: @tabBg@;
    border: 1px solid @tabBorder@;
    border-bottom: none;
    border-top-left-radius: 3px;
    border-top-right-radius: 3px;
    padding: 3px 10px;
    margin-right: 1px;
}
QTabBar::tab:selected { background: @tabSelBg@; margin-bottom: -1px; }
QTabBar::tab:hover:!selected { background: @hover@; }
QTabWidget::pane { border: 1px solid @tabBorder@; background: @window@; }

/* ---- group boxes and frames ------------------------------------------- */
QGroupBox {
    background: @window@;
    border: 1px solid @tabBorder@;
    border-radius: 3px;
    margin-top: 9px;
    padding-top: 6px;
    font-weight: bold;
}
QGroupBox::title {
    subcontrol-origin: margin;
    subcontrol-position: top left;
    left: 7px;
    padding: 0 4px;
    background: @window@;
}
QFrame[frameShape="4"], QFrame[frameShape="5"] { color: @sep@; background: @window@; }

/* ---- text fields ------------------------------------------------------ */
QLineEdit, QTextEdit, QPlainTextEdit {
    background-color: @base@;
    border: 1px solid @border@;
    border-radius: 2px;
    padding: 1px 3px;
    selection-background-color: @check@;
    selection-color: @text@;
}
QLineEdit:focus, QTextEdit:focus, QPlainTextEdit:focus { border-color: @accentDeep@; }
QLineEdit:read-only, QTextEdit:read-only { background-color: @disabledBg@; }

/* ---- status bar ------------------------------------------------------- */
QStatusBar {
    background-color: @window@;
    border-top: 1px solid @borderSoft@;
    color: @windowText@;
}
QStatusBar QLabel { background: transparent; }
QStatusBar::item { border: none; }

/* ---- splitters -------------------------------------------------------- */
QSplitter::handle { background: @borderFaint@; }
QSplitter::handle:horizontal { width: 3px; }
QSplitter::handle:vertical { height: 3px; }
QSplitter::handle:hover { background: @hoverBorder@; }

/* ---- progress --------------------------------------------------------- */
QProgressBar {
    background: @base@;
    border: 1px solid @border@;
    border-radius: 2px;
    text-align: center;
    height: 14px;
}
QProgressBar::chunk { background: @accentSoft@; }
)CSS");
}

bool systemPrefersDark()
{
    if (QStyleHints* hints = QGuiApplication::styleHints()) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
        if (hints->colorScheme() == Qt::ColorScheme::Dark)
            return true;
        if (hints->colorScheme() == Qt::ColorScheme::Light)
            return false;
#endif
    }
    // Older platforms: fall back to the lightness of the system window colour.
    const QColor c = QGuiApplication::palette().color(QPalette::Window);
    return c.isValid() && c.lightness() < 128;
}

ThemeMode loadMode()
{
    QSettings s;
    const int v = s.value(QStringLiteral("ui/themeMode"), int(ThemeMode::System)).toInt();
    if (v == int(ThemeMode::Light) || v == int(ThemeMode::Dark))
        return ThemeMode(v);
    return ThemeMode::System;
}

bool g_applied = false;
bool g_dark = false;
/// Guards against re-entry: install() sends PaletteChange to every widget, and
/// the event filter below would otherwise call back into install() forever.
bool g_installing = false;

/// Catches the desktop switching between light and dark on platforms that do
/// not expose QStyleHints::colorSchemeChanged.
class ThemeEventFilter : public QObject
{
public:
    bool eventFilter(QObject* obj, QEvent* e) override
    {
        // Only ThemeChange, never PaletteChange: our own setPalette() is what
        // delivers the latter, and reacting to it re-entered install().
        if (e->type() == QEvent::ThemeChange && !g_installing)
            syncThemeWithSystem();
        return QObject::eventFilter(obj, e);
    }
};

void install(bool dark)
{
    if (g_installing)
        return;
    g_installing = true;
    QScopeGuard done([] { g_installing = false; });
    g_dark = dark;

    // A matching QPalette first, so anything the stylesheet does not reach is
    // still consistent with the variant in use.
    const QHash<QString, QString> c = dark ? darkColours() : lightColours();
    const auto col = [&c](const char* key) { return QColor(c.value(QLatin1String(key))); };

    QPalette pal;
    pal.setColor(QPalette::Window, col("@window@"));
    pal.setColor(QPalette::WindowText, col("@windowText@"));
    pal.setColor(QPalette::Base, col("@base@"));
    pal.setColor(QPalette::AlternateBase, col("@altBase@"));
    pal.setColor(QPalette::Text, col("@text@"));
    pal.setColor(QPalette::Button, col("@button@"));
    pal.setColor(QPalette::ButtonText, col("@buttonText@"));
    pal.setColor(QPalette::Highlight, col("@check@"));
    pal.setColor(QPalette::HighlightedText, col("@text@"));
    pal.setColor(QPalette::ToolTipBase, col("@tooltip@"));
    pal.setColor(QPalette::ToolTipText, col("@windowText@"));
    pal.setColor(QPalette::PlaceholderText, col("@placeholder@"));
    pal.setColor(QPalette::Link, col("@link@"));
    pal.setColor(QPalette::Disabled, QPalette::Text, col("@disabled@"));
    pal.setColor(QPalette::Disabled, QPalette::ButtonText, col("@disabled@"));
    pal.setColor(QPalette::Disabled, QPalette::WindowText, col("@disabled@"));

    // The derived roles have to be set too. Left at their defaults they stay
    // light in a dark palette, which is what left the palette title bars and
    // some labels near-white on a dark background.
    const QColor window = col("@window@");
    const QColor base = col("@base@");
    const int step = dark ? 22 : 16;
    const auto shade = [dark, step](const QColor& c, int n) {
        return dark ? c.lighter(100 + n) : c.darker(100 + n);
    };
    pal.setColor(QPalette::Light, shade(window, step));
    pal.setColor(QPalette::Midlight, shade(window, step * 2));
    pal.setColor(QPalette::Mid, shade(window, step * 3));
    pal.setColor(QPalette::Dark, shade(window, step * 4));
    pal.setColor(QPalette::Shadow, dark ? QColor(0x0d, 0x0d, 0x0d) : QColor(0x8a, 0x8a, 0x8a));
    pal.setColor(QPalette::Base, base);
    pal.setColor(QPalette::AlternateBase, col("@altBase@"));
    pal.setColor(QPalette::ToolTipBase, col("@tooltip@"));
    pal.setColor(QPalette::Accent, col("@accent@"));
    pal.setColor(QPalette::LinkVisited, col("@link@"));
    QApplication::setPalette(pal);

    if (!qApp->style() || qApp->style()->objectName() != QLatin1String("fusion")) {
        if (QStyle* fusion = QStyleFactory::create(QStringLiteral("Fusion")))
            QApplication::setStyle(fusion);
    }

    QString sheet = styleSheetTemplate();
    for (auto it = c.cbegin(); it != c.cend(); ++it)
        sheet.replace(it.key(), it.value());
    qApp->setStyleSheet(sheet);

    // The procedural icons are drawn once and cached, so their ink has to follow
    // the theme as well; otherwise the line art disappears on a dark chrome.
    Icons::setInk(QColor(dark ? 0xe8 : 0x1e, dark ? 0xe8 : 0x1e, dark ? 0xe8 : 0x1e));
    Icons::clearCache();
}

} // namespace

QString styleSheetFor(bool dark)
{
    const QHash<QString, QString> c = dark ? darkColours() : lightColours();
    QString sheet = styleSheetTemplate();
    for (auto it = c.cbegin(); it != c.cend(); ++it)
        sheet.replace(it.key(), it.value());
    return sheet;
}

bool isDarkThemeInEffect()
{
    return g_dark;
}

ThemeMode themeMode()
{
    return loadMode();
}

void setThemeMode(ThemeMode mode)
{
    QSettings s;
    s.setValue(QStringLiteral("ui/themeMode"), int(mode));
    const bool dark = (mode == ThemeMode::Dark)
                      || (mode == ThemeMode::System && systemPrefersDark());
    if (dark != g_dark || !g_applied)
        install(dark);
}

void syncThemeWithSystem()
{
    if (loadMode() != ThemeMode::System || !g_applied)
        return;
    const bool dark = systemPrefersDark();
    if (dark != g_dark)
        install(dark);
}

void applyTheme()
{
    static std::once_flag once;
    std::call_once(once, [] {
        const bool dark = systemPrefersDark();
        install(dark);
        g_applied = true;
        // Follow the desktop setting while it changes. Qt 6.8 reports this
        // through QStyleHints; older platforms only send QEvent::ThemeChange.
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
        if (QStyleHints* hints = QGuiApplication::styleHints()) {
            QObject::connect(hints, &QStyleHints::colorSchemeChanged, qApp,
                             [] { syncThemeWithSystem(); });
        }
#endif
        // Older platforms have no style hint signal and only deliver
        // QEvent::ThemeChange to the application, so watch that too.
        qApp->installEventFilter(new ThemeEventFilter);
    });
}

} // namespace pnq
