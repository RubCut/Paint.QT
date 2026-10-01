#pragma once

#include "core/Document.h"
#include "core/Gradient.h"
#include "io/RecentFiles.h"
#include "ui/CanvasView.h"
#include "ui/ColorPane.h"
#include "ui/GradientPane.h"
#include "ui/HistoryPane.h"
#include "ui/LayersPane.h"
#include "ui/PropertyPane.h"
#include "ui/StatusPane.h"
#include "ui/ToolOptions.h"
#include "tools/ToolManager.h"

#include <QMainWindow>
#include <QMap>

class QScrollArea;
class QToolBar;
class QDockWidget;
class QListWidget;
class QShortcut;
class QLabel;
class QMenu;

namespace pnq {

class PalettePanel;
class ClipboardBridge;
class BrushesPane;
class ShapesPane;
class TextPane;

/// The application main window: menus, toolbars, palettes and the canvas.
class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

    /// Replaces the current document with the file at `path`.
    bool openFile(const QString& path);
    /// Opens a file in a brand new window.
    void openInNewWindow(const QString& path);
    /// Puts the pinned palette panels back in their corners right away.
    void reanchorPalettes();

    Document* document() const { return m_doc; }

    // ------------------------------------------------------------- commands
    void newImage();
    bool save();
    bool saveAs();
    bool exportAs();
    bool importFromFile();
    bool saveSelection();
    bool loadSelection();
    void print();

    void undo();
    void redo();
    void cut();
    void copy();
    void copyMerged();
    void paste();
    void pasteIntoNewLayer();
    void clearSelection();
    void deleteSelection();
    void selectAll();
    void deselectAll();
    void invertSelection();

    void resizeImage();
    void resizeCanvas();
    void rotateImage();
    /// Rotates the whole image by `degrees` (used by the "Rotate 180°" action).
    void rotateImage(double degrees);
    void rotateImageLeft();
    void rotateImageRight();
    void flipImageHorizontal();
    void flipImageVertical();
    void rotateLayer();
    void flipLayerHorizontal();
    void flipLayerVertical();
    void cropToSelection();
    void cropToLayer();
    void trim();
    void addLayer();
    void centerLayer();
    void mergeVisible();
    void flattenImage();
    void openAdjustments();
    /// Opens the adjustment dialog whose object name is `id`.
    void openAdjustmentById(const QString& id);
    void openEffects();
    void openPreferences();
    void showAbout();
    void updateRecentFiles();
    void status(const QString& text, int timeout = 4000);

public slots:
    void onActiveToolChanged();
    void refreshWindowTitle();
    void setToolById(const QString& id);

protected:
    void showEvent(QShowEvent* e) override;
    void resizeEvent(QResizeEvent* e) override;
    /// The window manager is dragging the window; keep quiet until it settles.
    void moveEvent(QMoveEvent* e) override;
    /// Connected to the canvas resize so the corner palettes follow it.
    void onCanvasResized();
    /// Builds the three floating palette panels and hides their dock versions.
    void buildPalettePanels();
    void closeEvent(QCloseEvent* e) override;
    void dragEnterEvent(QDragEnterEvent* e) override;
    void dropEvent(QDropEvent* e) override;
    void keyPressEvent(QKeyEvent* e) override;

private slots:
    void onDocumentDirtyChanged(bool dirty);
    void onHistoryJump(int index);
    void onCursorMoved(const QPoint& pos);
    void onToolOptionsChanged();
    void onPaletteVisibilityToggled();

private:
    // ------------------------------------------------------------- setup
    void createDocument(int w, int h, bool singleLayer, const QColor& bg);
    void buildActions();
    void buildMenus();
    void buildToolBars();
    void buildPalettes();
    void buildShortcuts();
    void connectPanes();
    void applyPreferences();
    void restoreSettings();
    void saveSettings();
    QWidget* createToolOptionsPage();
    /// Re-pins the floating palettes to the window corners unless the user moved one.
    void anchorFloatingPalettes(bool force = false);
    /// Re-assigns the procedural icons after the theme changed their ink.
    void refreshIcons();
    /// Called after the theme was switched at runtime.
    void onThemeChanged();
    /// Pulls an image off the system clipboard into m_clipboard.
    bool importClipboardImage();
    void refreshToolOptions();
    /// Keeps the tool-options strip exactly as tall as the active tool needs.
    void resizeOptionsScroll();
    void updateActionStates();
    void updateWindowIcon();
    void pushRecentMenu();
    Surface clipboardPixels() const { return m_clipboard; }
    void setClipboardPixels(const Surface& s);
    /// Applies a full-surface effect/adjustment to the active layer with undo.
    void applyToActiveLayer(const QString& name,
                            const std::function<void(Surface&, const Selection&)>& fn,
                            bool preview = false);
    void applyLivePreview(const QString& name,
                          const std::function<void(Surface&, const Selection&)>& fn);

    // ------------------------------------------------------------- state
    Document* m_doc = nullptr;
    ToolManager* m_tools = nullptr;
    RecentFiles* m_recent = nullptr;

    CanvasView* m_canvas = nullptr;
    ToolOptionsBar* m_optionsBar = nullptr;
    StatusPane* m_status = nullptr;
    LayersPane* m_layers = nullptr;
    HistoryPane* m_history = nullptr;
    ColorPane* m_colors = nullptr;
    BrushesPane* m_brushes = nullptr;
    ShapesPane* m_shapes = nullptr;
    TextPane* m_text = nullptr;
    GradientPane* m_gradients = nullptr;
    PropertyPane* m_properties = nullptr;

    QToolBar* m_toolBar = nullptr;
    QToolBar* m_commandBar = nullptr;
    /// Floating in-window palette panels: name, panel, default corner.
    QList<PalettePanel*> m_panels;
    /// Action ids on the command bar, kept so their icons can be rebuilt.
    QStringList m_commandIds;
    PalettePanel* m_panelColors = nullptr;
    PalettePanel* m_panelLayers = nullptr;
    PalettePanel* m_panelHistory = nullptr;
    /// Canvas rect the panels were last placed against, so we only move them on a
    /// real change.
    QRect m_lastCanvasRect;
    /// True while the window manager is moving the window around.
    bool m_windowMoving = false;
    /// Debounces the end of a window move.
    class QTimer* m_moveSettle = nullptr;
    /// Tool whose sessionChanged signal currently drives the Finish button.
    Tool* m_finishConnected = nullptr;
    QHash<QString, QWidget*> m_paletteWidgets;
    QHash<QString, QDockWidget*> m_docks;
    QList<QDockWidget*> m_paletteDocks;

    bool m_fitted = false;
    Surface m_clipboard;
    /// Reads and writes the system clipboard.
    ClipboardBridge* m_clip = nullptr;
    bool m_clipboardMerged = false;
    QPoint m_pasteOffset = { 0, 0 };
    QString m_lastDir;

    // Live preview state for adjustments / effects.
    bool m_livePreview = false;
    int m_liveLayer = -1;
    Surface m_liveOriginal;

    // Actions
    QHash<QString, QAction*> m_actions;
    QList<QShortcut*> m_shortcuts;
    QMap<QString, QString> m_toolShortcuts;
    QMap<QString, QString> m_commandShortcuts;

    QMenu* m_recentMenu = nullptr;
    bool m_updatingPalette = false;
};

} // namespace pnq
