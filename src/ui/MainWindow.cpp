#include "ui/MainWindow.h"
#include "ui/ClipboardBridge.h"
#include "ui/PalettePanel.h"
#include "ui/Theme.h"

#include "core/Brush.h"
#include "core/History.h"
#include "core/ImageMath.h"
#include "core/ImageOps.h"
#include "core/Renderer.h"
#include "effects/Effects.h"
#include "io/FileFormats.h"
#include "io/Pdn3Reader.h"
#include "io/PdqFile.h"
#include <type_traits>

#include "resources/Icons.h"
#include "tools/Tool.h"
#include "ui/AdjustmentsDialog.h"
#include "ui/BlurDialog.h"
#include "ui/BrushesPane.h"
#include "ui/CanvasView.h"
#include "ui/ColorPane.h"
#include "ui/EffectsDialog.h"
#include "ui/LayersPane.h"
#include "ui/NoiseDialog.h"
#include "ui/PropertyPane.h"
#include "ui/ShapesPane.h"
#include "ui/StylizeDialog.h"
#include "ui/dialogs/Dialogs.h"
#include "ui/dialogs/AboutDialog.h"
#include "ui/dialogs/GradientEditorDialog.h"
#include "ui/dialogs/PreferencesDialog.h"
#include "ui/dialogs/ShortcutDisplayWidget.h"
#include "ui/dialogs/ShortcutEditorDialog.h"

#include <QActionGroup>
#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QDockWidget>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QInputDialog>
#include <QLabel>
#include <QListWidget>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QPainter>
#include <QSettings>
#include <QShowEvent>
#include <QSpinBox>
#include <QStandardPaths>
#include <QStatusBar>
#include <QScrollArea>
#include <QTimer>
#include <QToolBar>
#include <QVBoxLayout>

#ifdef PNQ_PRINT_SUPPORT
#include <QPageSetupDialog>
#include <QPrintDialog>
#include <QPrintPreviewDialog>
#include <QPrintPreviewWidget>
#include <QPrinter>
#endif

namespace pnq {

namespace {
/// Creates a QAction, connects it and (optionally) adds it to a menu/toolbar.
template <typename Owner, typename Slot>
QAction* makeAction(Owner* owner, QMenu* menu, QToolBar* bar, const QString& id, const QString& text,
                    const QKeySequence& shortcut, Slot slot)
{
    auto* a = new QAction(text, owner);
    a->setObjectName(id);
    if (!shortcut.isEmpty())
        a->setShortcut(shortcut);
    // `slot` arrives as either a member function pointer or a lambda taking no
    // arguments, and neither can be handed to connect() in a way that works
    // across Qt versions: the four argument overload that accepts a member
    // function pointer together with a context object does not exist before
    // Qt 6.9, and a bare pointer needs the object to call it on. Wrapping in a
    // generic lambda works on every version. The generic parameter swallows
    // triggered()'s `checked` argument, which these slots do not want anyway.
    if constexpr (std::is_member_function_pointer_v<Slot>) {
        QObject::connect(a, &QAction::triggered, owner,
                         [owner, slot](auto&&...) { (owner->*slot)(); });
    } else {
        QObject::connect(a, &QAction::triggered, owner, [slot](auto&&...) { slot(); });
    }
    if (menu)
        menu->addAction(a);
    if (bar)
        bar->addAction(a);
    return a;
}
} // namespace

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent)
{
    // Paint.NET draws its own light chrome; applied here so every entry point,
    // including the test harnesses, gets the same look.
    applyTheme();
    setWindowTitle(tr("Untitled - Paint.QT"));
    setWindowIcon(Icons::app());
    setDockOptions(QMainWindow::AnimatedDocks | QMainWindow::AllowTabbedDocks
                   | QMainWindow::AllowNestedDocks);
    setAcceptDrops(true);

    m_recent = new RecentFiles(this);
    m_tools = new ToolManager(this);
    m_lastDir = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);

    // ----- central widget: options bar + canvas -----
    QWidget* central = new QWidget(this);
    QVBoxLayout* cl = new QVBoxLayout(central);
    cl->setContentsMargins(0, 0, 0, 0);
    cl->setSpacing(0);
    // ToolOptionsBar scrolls its own page internally, so the bar itself only
    // imposes the height of one row and never widens the window.
    m_optionsBar = new ToolOptionsBar(this);
    m_optionsBar->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    cl->addWidget(m_optionsBar);
    m_canvas = new CanvasView(this);
    cl->addWidget(m_canvas, 1);
    setCentralWidget(central);
    setMinimumSize(880, 560);

    m_status = new StatusPane(this);
    statusBar()->addWidget(m_status, 1);

    // ----- document -----
    createDocument(800, 600, true, QColor(Qt::white));

    buildActions();
    // The tool bars must exist before the menus: the Window menu uses
    // QToolBar::toggleViewAction() of the options bar.
    buildToolBars();
    buildMenus();
    buildPalettes();
    buildShortcuts();
    // Bridge the system clipboard so images travel in and out of the process.
    m_clip = new ClipboardBridge(QApplication::clipboard(), this);
    m_clip->attach(m_doc);
    connect(m_clip, &ClipboardBridge::changed, this, [this] {
        if (importClipboardImage()) {
            m_pasteOffset = QPoint(0, 0);
            updateActionStates();
        }
    });
    connectPanes();
    // Preferences are applied here rather than at the end of buildMenus: they
    // touch the palette panes, which do not exist until buildPalettes has run.
    applyPreferences();
    restoreSettings();
    setToolById(QStringLiteral("pencil"));
    updateActionStates();
}

MainWindow::~MainWindow()
{
    if (m_doc)
        m_doc->deleteLater();
}

// ------------------------------------------------------------------ document

void MainWindow::createDocument(int w, int h, bool singleLayer, const QColor& bg)
{
    if (m_doc) {
        m_doc->disconnect(this);
        m_doc->deleteLater();
    }
    m_doc = new Document(w, h, this);
    Layer* layer = m_doc->addLayer(singleLayer ? tr("Background") : tr("Layer 1"));
    if (singleLayer) {
        layer->setBackground(true);
        Surface s(w, h);
        s.fill(toPixel(bg));
        layer->setSurface(s);
    }
    m_doc->setMaxHistoryLength(20);
    m_doc->history()->clear();
    m_doc->setDirty(false);

    m_tools->setDocument(m_doc);
    m_canvas->setDocument(m_doc);
    m_canvas->zoomToFit();
    if (m_layers)
        m_layers->setDocument(m_doc);
    if (m_history)
        m_history->setHistory(m_doc->history());
    if (m_status)
        m_status->setDocument(m_doc);
    if (m_properties)
        m_properties->setDocument(m_doc);

    connect(m_doc, &Document::dirtyChanged, this, &MainWindow::onDocumentDirtyChanged);
    connect(m_doc, &Document::historyChanged, this, [this] { updateActionStates(); });
    connect(m_doc, &Document::layersChanged, this, &MainWindow::updateActionStates);
    connect(m_doc, &Document::activeLayerChanged, this, [this](int) { updateActionStates(); });
    connect(m_doc, &Document::selectionChanged, this, [this] {
        if (m_properties)
            m_properties->setSelectionInfo();
        updateActionStates();
    });
    connect(m_doc, &Document::canvasSizeChanged, this, [this] { refreshWindowTitle(); });

    connect(m_doc->history(), &History::changed, this, &MainWindow::updateActionStates);

    refreshWindowTitle();
    updateActionStates();
}

// ------------------------------------------------------------------ actions

void MainWindow::buildActions()
{
    auto reg = [this](const QString& id, QAction* a) {
        m_actions.insert(id, a);
        return a;
    };

    // File
    reg(QStringLiteral("file.new"),
        makeAction(this, nullptr, nullptr, QStringLiteral("file.new"), tr("&New..."),
                  QKeySequence::New, &MainWindow::newImage));
    reg(QStringLiteral("file.open"),
        makeAction(this, nullptr, nullptr, QStringLiteral("file.open"), tr("&Open..."),
                  QKeySequence::Open, [this] {
                      const QString path = QFileDialog::getOpenFileName(
                          this, tr("Open"), m_lastDir, FileFormats::openFilters().join(QLatin1Char('\n')));
                      if (!path.isEmpty()) {
                          m_lastDir = QFileInfo(path).absolutePath();
                          openFile(path);
                      }
                  }));
    reg(QStringLiteral("file.save"),
        makeAction(this, nullptr, nullptr, QStringLiteral("file.save"), tr("&Save"),
                  QKeySequence::Save, &MainWindow::save));
    reg(QStringLiteral("file.saveas"),
        makeAction(this, nullptr, nullptr, QStringLiteral("file.saveas"), tr("Save &As..."),
                  QKeySequence(QStringLiteral("Ctrl+Shift+S")), &MainWindow::saveAs));
    reg(QStringLiteral("file.import"),
        makeAction(this, nullptr, nullptr, QStringLiteral("file.import"), tr("&Import From File..."),
                  QKeySequence(QStringLiteral("Ctrl+I")), &MainWindow::importFromFile));
    reg(QStringLiteral("file.export"),
        makeAction(this, nullptr, nullptr, QStringLiteral("file.export"), tr("&Export To File..."),
                  QKeySequence(QStringLiteral("Ctrl+E")), &MainWindow::exportAs));
    reg(QStringLiteral("file.print"),
        makeAction(this, nullptr, nullptr, QStringLiteral("file.print"), tr("&Print..."),
                  QKeySequence::Print, &MainWindow::print));
    reg(QStringLiteral("file.printpreview"),
        makeAction(this, nullptr, nullptr, QStringLiteral("file.printpreview"), tr("Print Pre&view..."),
                  QKeySequence(QStringLiteral("Ctrl+P")), &MainWindow::print));
    reg(QStringLiteral("file.saveselection"),
        makeAction(this, nullptr, nullptr, QStringLiteral("file.saveselection"),
                  tr("Save &Selection..."), QKeySequence(), &MainWindow::saveSelection));
    reg(QStringLiteral("file.loadselection"),
        makeAction(this, nullptr, nullptr, QStringLiteral("file.loadselection"),
                  tr("Loa&d Selection..."), QKeySequence(), &MainWindow::loadSelection));
    reg(QStringLiteral("file.quit"),
        makeAction(this, nullptr, nullptr, QStringLiteral("file.quit"), tr("E&xit"),
                  QKeySequence::Quit, [this] { close(); }));

    // Edit
    reg(QStringLiteral("edit.undo"),
        makeAction(this, nullptr, nullptr, QStringLiteral("edit.undo"), tr("&Undo"),
                  QKeySequence::Undo, &MainWindow::undo));
    reg(QStringLiteral("edit.redo"),
        makeAction(this, nullptr, nullptr, QStringLiteral("edit.redo"), tr("&Redo"),
                  QKeySequence(QStringLiteral("Ctrl+Y")), &MainWindow::redo));
    reg(QStringLiteral("edit.cut"),
        makeAction(this, nullptr, nullptr, QStringLiteral("edit.cut"), tr("Cu&t"),
                  QKeySequence::Cut, &MainWindow::cut));
    reg(QStringLiteral("edit.copy"),
        makeAction(this, nullptr, nullptr, QStringLiteral("edit.copy"), tr("&Copy"),
                  QKeySequence::Copy, &MainWindow::copy));
    reg(QStringLiteral("edit.copymerged"),
        makeAction(this, nullptr, nullptr, QStringLiteral("edit.copymerged"), tr("Copy &Merged"),
                  QKeySequence(QStringLiteral("Ctrl+Shift+C")), &MainWindow::copyMerged));
    reg(QStringLiteral("edit.paste"),
        makeAction(this, nullptr, nullptr, QStringLiteral("edit.paste"), tr("&Paste"),
                  QKeySequence::Paste, &MainWindow::paste));
    reg(QStringLiteral("edit.pastenewlayer"),
        makeAction(this, nullptr, nullptr, QStringLiteral("edit.pastenewlayer"),
                  tr("Paste Into &New Layer"), QKeySequence(QStringLiteral("Ctrl+Shift+V")),
                  &MainWindow::pasteIntoNewLayer));
    reg(QStringLiteral("edit.clear"),
        makeAction(this, nullptr, nullptr, QStringLiteral("edit.clear"), tr("C&lear"),
                  QKeySequence(QStringLiteral("Delete")), &MainWindow::clearSelection));
    reg(QStringLiteral("edit.selectall"),
        makeAction(this, nullptr, nullptr, QStringLiteral("edit.selectall"), tr("Select &All"),
                  QKeySequence::SelectAll, &MainWindow::selectAll));
    reg(QStringLiteral("edit.deselectall"),
        makeAction(this, nullptr, nullptr, QStringLiteral("edit.deselectall"), tr("&Deselect All"),
                  QKeySequence(QStringLiteral("Ctrl+Shift+A")), &MainWindow::deselectAll));
    reg(QStringLiteral("edit.invertselection"),
        makeAction(this, nullptr, nullptr, QStringLiteral("edit.invertselection"),
                  tr("&Invert Selection"), QKeySequence(QStringLiteral("Ctrl+Shift+I")),
                  &MainWindow::invertSelection));

    // Image
    reg(QStringLiteral("image.canvas"),
        makeAction(this, nullptr, nullptr, QStringLiteral("image.canvas"), tr("&Canvas Size..."),
                  QKeySequence(QStringLiteral("Ctrl+Alt+C")), &MainWindow::resizeCanvas));
    reg(QStringLiteral("image.image"),
        makeAction(this, nullptr, nullptr, QStringLiteral("image.image"), tr("&Image Size..."),
                  QKeySequence(QStringLiteral("Ctrl+I")), &MainWindow::resizeImage));
    reg(QStringLiteral("image.rotate180"),
        makeAction(this, nullptr, nullptr, QStringLiteral("image.rotate180"), tr("Rotate 180°"),
                  QKeySequence(), [this] { rotateImage(180.0); }));
    reg(QStringLiteral("image.rotate90cw"),
        makeAction(this, nullptr, nullptr, QStringLiteral("image.rotate90cw"),
                  tr("Rotate 90° Clockwise"), QKeySequence(), &MainWindow::rotateImageRight));
    reg(QStringLiteral("image.rotate90ccw"),
        makeAction(this, nullptr, nullptr, QStringLiteral("image.rotate90ccw"),
                  tr("Rotate 90° Counter-Clockwise"), QKeySequence(),
                  &MainWindow::rotateImageLeft));
    reg(QStringLiteral("image.rotatearbitrary"),
        makeAction(this, nullptr, nullptr, QStringLiteral("image.rotatearbitrary"),
                   tr("Rotate &Arbitrary..."), QKeySequence(QStringLiteral("Ctrl+Alt+R")),
                   [this] { rotateImage(); }));
    reg(QStringLiteral("image.flipH"),
        makeAction(this, nullptr, nullptr, QStringLiteral("image.flipH"),
                  tr("Flip &Horizontal"), QKeySequence(), &MainWindow::flipImageHorizontal));
    reg(QStringLiteral("image.flipV"),
        makeAction(this, nullptr, nullptr, QStringLiteral("image.flipV"),
                  tr("Flip &Vertical"), QKeySequence(), &MainWindow::flipImageVertical));
    reg(QStringLiteral("image.croptoselection"),
        makeAction(this, nullptr, nullptr, QStringLiteral("image.croptoselection"),
                  tr("Crop to &Selection"), QKeySequence(), &MainWindow::cropToSelection));
    reg(QStringLiteral("image.croptolayer"),
        makeAction(this, nullptr, nullptr, QStringLiteral("image.croptolayer"), tr("Crop to Layer"),
                  QKeySequence(), &MainWindow::cropToLayer));
    reg(QStringLiteral("image.trim"),
        makeAction(this, nullptr, nullptr, QStringLiteral("image.trim"), tr("&Trim"),
                  QKeySequence(), &MainWindow::trim));

    // Layers
    reg(QStringLiteral("layers.add"),
        makeAction(this, nullptr, nullptr, QStringLiteral("layers.add"), tr("Add New &Layer"),
                   QKeySequence(QStringLiteral("Ctrl+Shift+N")), [this] { addLayer(); }));
    reg(QStringLiteral("layers.delete"),
        makeAction(this, nullptr, nullptr, QStringLiteral("layers.delete"), tr("Delete Layer"),
                  QKeySequence(QStringLiteral("Ctrl+Q")),
                  [this] {
                      if (m_layers)
                          m_layers->deleteLayer();
                  }));
    reg(QStringLiteral("layers.duplicate"),
        makeAction(this, nullptr, nullptr, QStringLiteral("layers.duplicate"),
                  tr("Duplicate Layer"), QKeySequence(QStringLiteral("Ctrl+D")),
                  [this] {
                      if (m_layers)
                          m_layers->duplicateLayer();
                  }));
    reg(QStringLiteral("layers.properties"),
        makeAction(this, nullptr, nullptr, QStringLiteral("layers.properties"),
                  tr("Layer Properties..."), QKeySequence(QStringLiteral("Ctrl+L")),
                  [this] {
                      if (m_layers)
                          m_layers->showLayerPropertiesDialog();
                  }));
    reg(QStringLiteral("layers.mergeDown"),
        makeAction(this, nullptr, nullptr, QStringLiteral("layers.mergeDown"),
                  tr("&Merge Layer Down"), QKeySequence(QStringLiteral("Ctrl+E")),
                  [this] {
                      if (m_layers)
                          m_layers->mergeDown();
                  }));
    reg(QStringLiteral("layers.mergeVisible"),
        makeAction(this, nullptr, nullptr, QStringLiteral("layers.mergeVisible"),
                  tr("Merge &Visible Layers"), QKeySequence(QStringLiteral("Ctrl+Shift+E")),
                  &MainWindow::mergeVisible));
    reg(QStringLiteral("layers.flatten"),
        makeAction(this, nullptr, nullptr, QStringLiteral("layers.flatten"), tr("Flatten Image"),
                  QKeySequence(), &MainWindow::flattenImage));
    reg(QStringLiteral("layers.moveUp"),
        makeAction(this, nullptr, nullptr, QStringLiteral("layers.moveUp"), tr("Move Layer Up"),
                  QKeySequence(), [this] {
                      if (m_layers)
                          m_layers->moveLayerUp();
                  }));
    reg(QStringLiteral("layers.moveDown"),
        makeAction(this, nullptr, nullptr, QStringLiteral("layers.moveDown"), tr("Move Layer Down"),
                  QKeySequence(), [this] {
                      if (m_layers)
                          m_layers->moveLayerDown();
                  }));
    reg(QStringLiteral("layers.rotate"),
        makeAction(this, nullptr, nullptr, QStringLiteral("layers.rotate"), tr("Rotate Layer..."),
                  QKeySequence(), &MainWindow::rotateLayer));
    reg(QStringLiteral("layers.flipH"),
        makeAction(this, nullptr, nullptr, QStringLiteral("layers.flipH"), tr("Flip Layer Horizontal"),
                  QKeySequence(), &MainWindow::flipLayerHorizontal));
    reg(QStringLiteral("layers.flipV"),
        makeAction(this, nullptr, nullptr, QStringLiteral("layers.flipV"), tr("Flip Layer Vertical"),
                  QKeySequence(), &MainWindow::flipLayerVertical));
    reg(QStringLiteral("layers.center"),
        makeAction(this, nullptr, nullptr, QStringLiteral("layers.center"), tr("Center Layer"),
                  QKeySequence(QStringLiteral("Ctrl+Alt+E")), &MainWindow::centerLayer));

    // Select
    reg(QStringLiteral("select.all"),
        makeAction(this, nullptr, nullptr, QStringLiteral("select.all"), tr("Select All"),
                  QKeySequence(QStringLiteral("Ctrl+A")), &MainWindow::selectAll));
    reg(QStringLiteral("select.none"),
        makeAction(this, nullptr, nullptr, QStringLiteral("select.none"), tr("Deselect All"),
                  QKeySequence(QStringLiteral("Ctrl+Shift+A")), &MainWindow::deselectAll));
    reg(QStringLiteral("select.invert"),
        makeAction(this, nullptr, nullptr, QStringLiteral("select.invert"), tr("Invert Selection"),
                  QKeySequence(QStringLiteral("Ctrl+Shift+I")), &MainWindow::invertSelection));
    reg(QStringLiteral("select.grow"),
        makeAction(this, nullptr, nullptr, QStringLiteral("select.grow"), tr("Grow Selection"),
                  QKeySequence(), [this] { m_doc->growSelection(4); }));
    reg(QStringLiteral("select.shrink"),
        makeAction(this, nullptr, nullptr, QStringLiteral("select.shrink"),
                  tr("Contract Selection"), QKeySequence(), [this] { m_doc->contractSelection(4); }));
    reg(QStringLiteral("select.feather"),
        makeAction(this, nullptr, nullptr, QStringLiteral("select.feather"), tr("Feather Selection"),
                  QKeySequence(), [this] {
                      bool ok = false;
                      const int r = QInputDialog::getInt(this, tr("Feather Selection"),
                                                         tr("Amount:"), 4, 0, 100, 1, &ok);
                      if (ok)
                          m_doc->featherSelection(r);
                  }));
    reg(QStringLiteral("select.borderselection"),
        makeAction(this, nullptr, nullptr, QStringLiteral("select.borderselection"),
                  tr("Border Selection"), QKeySequence(), [this] { m_doc->borderSelection(4); }));

    // Zoom
    reg(QStringLiteral("view.zoomin"),
        makeAction(this, nullptr, nullptr, QStringLiteral("view.zoomin"), tr("Zoom &In"),
                  QKeySequence::ZoomIn, [this] { m_canvas->zoomIn(); }));
    reg(QStringLiteral("view.zoomout"),
        makeAction(this, nullptr, nullptr, QStringLiteral("view.zoomout"), tr("Zoom &Out"),
                  QKeySequence::ZoomOut, [this] { m_canvas->zoomOut(); }));
    reg(QStringLiteral("view.zoom100"),
        makeAction(this, nullptr, nullptr, QStringLiteral("view.zoom100"), tr("Zoom to &100%"),
                  QKeySequence(QStringLiteral("Ctrl+1")), [this] { m_canvas->zoomOriginal(); }));
    reg(QStringLiteral("view.zoomfit"),
        makeAction(this, nullptr, nullptr, QStringLiteral("view.zoomfit"), tr("&Fit On Screen"),
                  QKeySequence(QStringLiteral("Ctrl+0")), [this] { m_canvas->zoomToFit(); }));
    reg(QStringLiteral("view.zoomselection"),
        makeAction(this, nullptr, nullptr, QStringLiteral("view.zoomselection"),
                  tr("Zoom to &Selection"), QKeySequence(), [this] { m_canvas->zoomToSelection(); }));

    // Rulers / grid / guides
    reg(QStringLiteral("view.rulers"),
        makeAction(this, nullptr, nullptr, QStringLiteral("view.rulers"), tr("R&ulers"),
                  QKeySequence(QStringLiteral("Ctrl+R")), [this] {
                      const bool on = !m_canvas->rulersVisible();
                      m_canvas->setRulersVisible(on);
                      QSettings().setValue(QStringLiteral("ui/rulers"), on);
                  }));
    reg(QStringLiteral("view.grid"),
        makeAction(this, nullptr, nullptr, QStringLiteral("view.grid"), tr("Show &Grid"),
                  QKeySequence(QStringLiteral("Ctrl+G")), [this] {
                      m_canvas->setGridVisible(!m_canvas->gridVisible());
                  }));
    reg(QStringLiteral("view.snap"),
        makeAction(this, nullptr, nullptr, QStringLiteral("view.snap"),
                  tr("Snap to &Guides and Grid"), QKeySequence(), [this] {
                      m_canvas->setSnapToGuides(!m_canvas->snapToGuides());
                  }));
    reg(QStringLiteral("view.clearguides"),
        makeAction(this, nullptr, nullptr, QStringLiteral("view.clearguides"), tr("Clear Guides"),
                  QKeySequence(), [this] { m_canvas->clearGuides(); }));
    reg(QStringLiteral("view.hguide"),
        makeAction(this, nullptr, nullptr, QStringLiteral("view.hguide"), tr("Add &Horizontal Guide"),
                  QKeySequence(), [this] {
                      m_canvas->addHorizontalGuide(m_canvas->lastCursorImagePos().y());
                  }));
    reg(QStringLiteral("view.vguide"),
        makeAction(this, nullptr, nullptr, QStringLiteral("view.vguide"), tr("Add &Vertical Guide"),
                  QKeySequence(), [this] {
                      m_canvas->addVerticalGuide(m_canvas->lastCursorImagePos().x());
                  }));

    // Tools (keyboard-selected)
    for (Tool* t : m_tools->tools()) {
        const QString key = QStringLiteral("tool.") + t->id();
        auto* a = new QAction(t->name(), this);
        a->setObjectName(key);
        connect(a, &QAction::triggered, this, [this, id = t->id()] { setToolById(id); });
        m_actions.insert(key, a);
    }
    reg(QStringLiteral("colors.swap"),
        makeAction(this, nullptr, nullptr, QStringLiteral("colors.swap"), tr("Swap Colors"),
                  QKeySequence(QStringLiteral("X")), [this] { m_colors->swapColors(); }));
    reg(QStringLiteral("colors.default"),
        makeAction(this, nullptr, nullptr, QStringLiteral("colors.default"),
                  tr("Default Foreground/Background Colors"),
                  QKeySequence(QStringLiteral("D")), [this] { m_colors->setDefaultColors(); }));

    // Help / settings
    reg(QStringLiteral("help.preferences"),
        makeAction(this, nullptr, nullptr, QStringLiteral("help.preferences"), tr("&Preferences"),
                  QKeySequence(), &MainWindow::openPreferences));
    reg(QStringLiteral("help.shortcuts"),
        makeAction(this, nullptr, nullptr, QStringLiteral("help.shortcuts"),
                  tr("&Keyboard Shortcuts..."), QKeySequence(), [this] {
                      ShortcutEditorDialog dlg(this);
                      dlg.exec();
                  }));
    reg(QStringLiteral("help.about"),
        makeAction(this, nullptr, nullptr, QStringLiteral("help.about"), tr("&About Paint.QT"),
                  QKeySequence(), &MainWindow::showAbout));

    m_actions[QStringLiteral("view.rulers")]->setCheckable(true);
    m_actions[QStringLiteral("view.grid")]->setCheckable(true);
    m_actions[QStringLiteral("view.snap")]->setCheckable(true);
}

void MainWindow::buildMenus()
{
    QMenu* file = menuBar()->addMenu(tr("&File"));
    file->addAction(m_actions[QStringLiteral("file.new")]);
    file->addAction(m_actions[QStringLiteral("file.open")]);
    m_recentMenu = file->addMenu(tr("Open &Recent"));
    file->addSeparator();
    file->addAction(m_actions[QStringLiteral("file.save")]);
    file->addAction(m_actions[QStringLiteral("file.saveas")]);
    file->addSeparator();
    file->addAction(m_actions[QStringLiteral("file.import")]);
    file->addAction(m_actions[QStringLiteral("file.export")]);
    file->addSeparator();
    file->addAction(m_actions[QStringLiteral("file.print")]);
    file->addAction(m_actions[QStringLiteral("file.printpreview")]);
    file->addSeparator();
    file->addAction(m_actions[QStringLiteral("file.saveselection")]);
    file->addAction(m_actions[QStringLiteral("file.loadselection")]);
    file->addSeparator();
    file->addAction(m_actions[QStringLiteral("file.quit")]);
    connect(m_recent, &RecentFiles::changed, this, &MainWindow::updateRecentFiles);

    QMenu* edit = menuBar()->addMenu(tr("&Edit"));
    edit->addAction(m_actions[QStringLiteral("edit.undo")]);
    edit->addAction(m_actions[QStringLiteral("edit.redo")]);
    edit->addSeparator();
    edit->addAction(m_actions[QStringLiteral("edit.cut")]);
    edit->addAction(m_actions[QStringLiteral("edit.copy")]);
    edit->addAction(m_actions[QStringLiteral("edit.copymerged")]);
    edit->addAction(m_actions[QStringLiteral("edit.paste")]);
    edit->addAction(m_actions[QStringLiteral("edit.pastenewlayer")]);
    edit->addSeparator();
    edit->addAction(m_actions[QStringLiteral("edit.clear")]);
    edit->addSeparator();

    QMenu* select = menuBar()->addMenu(tr("Se&lect"));
    select->addAction(m_actions[QStringLiteral("select.all")]);
    select->addAction(m_actions[QStringLiteral("select.none")]);
    select->addAction(m_actions[QStringLiteral("select.invert")]);
    select->addSeparator();
    select->addAction(m_actions[QStringLiteral("select.grow")]);
    select->addAction(m_actions[QStringLiteral("select.shrink")]);
    select->addAction(m_actions[QStringLiteral("select.feather")]);
    select->addAction(m_actions[QStringLiteral("select.borderselection")]);

    QMenu* image = menuBar()->addMenu(tr("&Image"));
    image->addAction(m_actions[QStringLiteral("image.canvas")]);
    image->addAction(m_actions[QStringLiteral("image.image")]);
    image->addSeparator();
    image->addAction(m_actions[QStringLiteral("image.rotate180")]);
    image->addAction(m_actions[QStringLiteral("image.rotate90cw")]);
    image->addAction(m_actions[QStringLiteral("image.rotate90ccw")]);
    image->addAction(m_actions[QStringLiteral("image.rotatearbitrary")]);
    image->addAction(m_actions[QStringLiteral("image.flipH")]);
    image->addAction(m_actions[QStringLiteral("image.flipV")]);
    image->addSeparator();
    image->addAction(m_actions[QStringLiteral("image.croptoselection")]);
    image->addAction(m_actions[QStringLiteral("image.croptolayer")]);
    image->addAction(m_actions[QStringLiteral("image.trim")]);

    QMenu* layers = menuBar()->addMenu(tr("&Layers"));
    layers->addAction(m_actions[QStringLiteral("layers.add")]);
    layers->addAction(m_actions[QStringLiteral("layers.delete")]);
    layers->addAction(m_actions[QStringLiteral("layers.duplicate")]);
    layers->addAction(m_actions[QStringLiteral("layers.properties")]);
    layers->addSeparator();
    layers->addAction(m_actions[QStringLiteral("layers.mergeDown")]);
    layers->addAction(m_actions[QStringLiteral("layers.mergeVisible")]);
    layers->addAction(m_actions[QStringLiteral("layers.flatten")]);
    layers->addSeparator();
    layers->addAction(m_actions[QStringLiteral("layers.moveUp")]);
    layers->addAction(m_actions[QStringLiteral("layers.moveDown")]);
    layers->addSeparator();
    layers->addAction(m_actions[QStringLiteral("layers.rotate")]);
    layers->addAction(m_actions[QStringLiteral("layers.flipH")]);
    layers->addAction(m_actions[QStringLiteral("layers.flipV")]);
    layers->addAction(m_actions[QStringLiteral("layers.center")]);

    // Adjustments submenu, mirroring Paint.NET's menu.
    QMenu* adjustments = menuBar()->addMenu(tr("&Adjustments"));
    const QVector<QString> adjIds = {
        QStringLiteral("adj.brightness"),   QStringLiteral("adj.curves"),
        QStringLiteral("adj.levels"),       QStringLiteral("adj.huesat"),
        QStringLiteral("adj.colorbalance"), QStringLiteral("adj.desaturate"),
        QStringLiteral("adj.posterize"),    QStringLiteral("adj.threshold"),
        QStringLiteral("adj.selective"),    QStringLiteral("adj.temperature"),
        QStringLiteral("adj.replacecolor"), QStringLiteral("adj.invert"),
        QStringLiteral("adj.autolevels"),   QStringLiteral("adj.channelmixer"),
    };
    const QVector<QString> adjTexts = {
        tr("Brightness/Contrast..."), tr("Curves..."),        tr("Levels..."),
        tr("Hue/Saturation..."),      tr("Color Balance..."), tr("Desaturate..."),
        tr("Posterize..."),           tr("Threshold..."),     tr("Selective Color..."),
        tr("Temperature/Tint..."),    tr("Replace Color..."), tr("Invert"),
        tr("Auto-Adjust Levels"),     tr("Channel Mixer..."),
    };
    for (int i = 0; i < adjIds.size(); ++i) {
        QAction* a = new QAction(adjTexts[i], this);
        a->setObjectName(adjIds[i]);
        const QString id = adjIds[i];
        connect(a, &QAction::triggered, this, [this, id] { openAdjustmentById(id); });
        m_actions.insert(adjIds[i], a);
        adjustments->addAction(a);
    }
    adjustments->addSeparator();
    adjustments->addAction(m_actions[QStringLiteral("edit.undo")]);

    QMenu* effects = menuBar()->addMenu(tr("Ef&fects"));
    QAction* aEffects = makeAction(this, effects, nullptr, QStringLiteral("effects.open"),
                                   tr("&Effects..."), QKeySequence(), &MainWindow::openEffects);
    Q_UNUSED(aEffects);
    effects->addAction(m_actions[QStringLiteral("edit.undo")]);

    QMenu* view = menuBar()->addMenu(tr("&View"));
    view->addAction(m_actions[QStringLiteral("view.zoomin")]);
    view->addAction(m_actions[QStringLiteral("view.zoomout")]);
    view->addAction(m_actions[QStringLiteral("view.zoom100")]);
    view->addAction(m_actions[QStringLiteral("view.zoomfit")]);
    view->addAction(m_actions[QStringLiteral("view.zoomselection")]);
    view->addSeparator();
    view->addAction(m_actions[QStringLiteral("view.rulers")]);
    view->addAction(m_actions[QStringLiteral("view.grid")]);
    view->addAction(m_actions[QStringLiteral("view.snap")]);
    view->addSeparator();
    view->addAction(m_actions[QStringLiteral("view.hguide")]);
    view->addAction(m_actions[QStringLiteral("view.vguide")]);
    view->addAction(m_actions[QStringLiteral("view.clearguides")]);
    view->addSeparator();

    // Follow the desktop light/dark setting by default, with a manual override.
    QMenu* theme = view->addMenu(tr("&Theme"));
    auto* themeGroup = new QActionGroup(this);
    const struct {
        const char* key;
        const char* label;
        ThemeMode mode;
    } themeChoices[] = {
        { "view.theme.system", QT_TRANSLATE_NOOP("MainWindow", "&Follow System"), ThemeMode::System },
        { "view.theme.light", QT_TRANSLATE_NOOP("MainWindow", "&Light"), ThemeMode::Light },
        { "view.theme.dark", QT_TRANSLATE_NOOP("MainWindow", "&Dark"), ThemeMode::Dark },
    };
    for (const auto& c : themeChoices) {
        QAction* a = makeAction(this, theme, nullptr, QString::fromLatin1(c.key),
                                tr(c.label), QKeySequence(),
                                [this, c] { setThemeMode(c.mode); onThemeChanged(); });
        a->setCheckable(true);
        themeGroup->addAction(a);
    }
    QMenu* window = menuBar()->addMenu(tr("&Window"));

    QMenu* help = menuBar()->addMenu(tr("&Help"));
    help->addAction(m_actions[QStringLiteral("help.about")]);
    help->addSeparator();
    help->addAction(m_actions[QStringLiteral("help.shortcuts")]);
    help->addAction(m_actions[QStringLiteral("help.preferences")]);
}

void MainWindow::onThemeChanged()
{
    // The procedural icons are cached in the theme's ink, so the actions have to
    // be handed the new ones, and the palettes raised over the canvas again.
    refreshIcons();
    QTimer::singleShot(0, this, [this] { anchorFloatingPalettes(true); });
}

void MainWindow::refreshIcons()
{
    for (Tool* t : m_tools->tools()) {
        if (QAction* a = m_actions.value(QStringLiteral("tool.") + t->id()))
            a->setIcon(t->icon().isNull() ? Icons::tool(t->id()) : t->icon());
    }
    for (const QString& id : m_commandIds) {
        if (!m_actions.contains(id))
            continue;
        QString verb = id;
        if (const int dot = verb.indexOf(QLatin1Char('.')); dot >= 0)
            verb = verb.mid(dot + 1);
        if (verb == QLatin1String("all"))
            verb = QStringLiteral("selectall");
        m_actions[id]->setIcon(Icons::command(verb));
    }
    if (m_toolBar)
        m_toolBar->update();
    if (m_commandBar)
        m_commandBar->update();
}

void MainWindow::buildToolBars()
{
    // ---- command bar: new / open / save / print / cut / copy / paste / undo ----
    m_commandBar = new QToolBar(tr("Commands"), this);
    m_commandBar->setObjectName(QStringLiteral("commandToolBar"));
    m_commandBar->setMovable(false);
    m_commandBar->setFloatable(false);
    m_commandBar->setIconSize(QSize(22, 22));
    m_commandBar->setToolButtonStyle(Qt::ToolButtonIconOnly);
    const char* const commandIds[] = {
        "file.new",  "file.open",  "file.save",  "file.print",
        "edit.cut",  "edit.copy",  "edit.paste", "edit.undo",
        "edit.redo", "select.all",
    };
    for (const char* id : commandIds) {
        m_commandIds << QString::fromLatin1(id);
        QAction* a = m_actions.value(QString::fromLatin1(id));
        if (!a)
            continue;
        // The action id is "file.save" but the artwork is keyed on the verb, so
        // strip the menu prefix before asking Icons for a drawing.
        QString verb = QString::fromLatin1(id);
        if (const int dot = verb.indexOf(QLatin1Char('.')); dot >= 0)
            verb = verb.mid(dot + 1);
        // "select.all" would otherwise ask for an "all" icon, which has none.
        if (verb == QLatin1String("all"))
            verb = QStringLiteral("selectall");
        a->setIcon(Icons::command(verb));
        m_commandBar->addAction(a);
    }
    // Keep text in the tooltip, drop it from the button itself.
    m_commandBar->setToolButtonStyle(Qt::ToolButtonIconOnly);
    addToolBar(Qt::TopToolBarArea, m_commandBar);
    m_commandBar->show();

    // ---- tool strip: a narrow vertical column on the left, like Paint.NET ----
    m_toolBar = new QToolBar(tr("Tools"), this);
    m_toolBar->setObjectName(QStringLiteral("toolsToolBar"));
    m_toolBar->setOrientation(Qt::Vertical);
    m_toolBar->setToolButtonStyle(Qt::ToolButtonIconOnly);
    m_toolBar->setIconSize(QSize(28, 28));
    m_toolBar->setFixedWidth(38);
    m_toolBar->setMovable(false);
    m_toolBar->setFloatable(false);
    addToolBar(Qt::LeftToolBarArea, m_toolBar);

    auto group = new QActionGroup(this);
    for (Tool* t : m_tools->tools()) {
        QAction* a = m_actions[QStringLiteral("tool.") + t->id()];
        a->setIcon(t->icon().isNull() ? Icons::tool(t->id()) : t->icon());
        a->setToolTip(QStringLiteral("%1 (%2)").arg(t->name(), t->shortcutString()));
        a->setCheckable(true);
        group->addAction(a);
        m_toolBar->addAction(a);
    }
    m_toolBar->show();

    // Tool Options is a strip in the central widget, so View > Tool Options
    // only has to show or hide that strip.
}

void MainWindow::buildPalettes()
{
    m_layers = new LayersPane(this);
    m_history = new HistoryPane(this);
    m_colors = new ColorPane(this);
    m_brushes = new BrushesPane(this);
    m_shapes = new ShapesPane(this);
    m_text = new TextPane(this);
    m_gradients = new GradientPane(this);
    m_properties = new PropertyPane(this);

    m_paletteWidgets = {
        { QStringLiteral("layers"), m_layers },
        { QStringLiteral("history"), m_history },
        { QStringLiteral("colors"), m_colors },
        { QStringLiteral("brushes"), m_brushes },
        { QStringLiteral("shapes"), m_shapes },
        { QStringLiteral("text"), m_text },
        { QStringLiteral("gradients"), m_gradients },
        { QStringLiteral("properties"), m_properties },
    };

    for (auto it = m_paletteWidgets.constBegin(); it != m_paletteWidgets.constEnd(); ++it) {
        // The map keys are lower-case identifiers; the titles are proper names.
        QString title = it.key();
        title[0] = title.at(0).toUpper();
        auto* dock = new QDockWidget(title, this);
        dock->setObjectName(QStringLiteral("palette_%1").arg(it.key()));
        dock->setWidget(it.value());
        dock->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable
                          | QDockWidget::DockWidgetClosable);
        addDockWidget(Qt::RightDockWidgetArea, dock);
        m_docks.insert(it.key(), dock);
        m_paletteDocks.append(dock);
    }

    // The three palettes Paint.NET shows by default live in floating panels; the
    // rest stay available from the Window menu as ordinary docks.
    buildPalettePanels();
}

void MainWindow::buildPalettePanels()
{
    if (!m_panelColors) {
        auto make = [this](const QString& key, PalettePanel** out) {
            QDockWidget* dock = m_docks.value(key);
            if (!dock || !dock->widget())
                return;
            *out = new PalettePanel(dock->windowTitle(), dock->widget(), this);
            (*out)->setObjectName(QStringLiteral("panel_") + key);
            // Keep each palette the size Paint.NET gives it, so none of them
            // grows into a column over the canvas.
            if (key == QLatin1String("colors"))
                (*out)->setSizeLimits(QSize(258, 300), QSize(300, 320));
            else if (key == QLatin1String("layers"))
                (*out)->setSizeLimits(QSize(300, 230), QSize(420, 300));
            else
                (*out)->setSizeLimits(QSize(250, 190), QSize(360, 260));
            // The dock itself stays as the Window menu's owner but is not shown;
            // its content widget now lives inside the panel.
            dock->hide();
            m_panels.append(*out);
        };
        make(QStringLiteral("history"), &m_panelHistory);
        make(QStringLiteral("layers"), &m_panelLayers);
        make(QStringLiteral("colors"), &m_panelColors);

        for (PalettePanel* p : m_panels) {
            connect(p, &PalettePanel::closeRequested, this, [this](PalettePanel* panel) {
                panel->hide();
                if (QAction* a = m_actions.value(QStringLiteral("window.palette.")
                                                 + panel->objectName().mid(6)))
                    a->setChecked(false);
            });
            // Once the user drags a panel it stays where they put it.
            connect(p, &PalettePanel::movedByUser, this, [this] { anchorFloatingPalettes(true); });
            connect(p, &PalettePanel::resizedByUser, this, [this] { anchorFloatingPalettes(true); });
        }
    }

    for (PalettePanel* p : m_panels)
        p->show();
    for (QDockWidget* d : m_paletteDocks) {
        if (d != m_docks.value(QStringLiteral("history"))
            && d != m_docks.value(QStringLiteral("layers"))
            && d != m_docks.value(QStringLiteral("colors")))
            d->setVisible(false);
    }
    anchorFloatingPalettes();
}

void MainWindow::reanchorPalettes()
{
    anchorFloatingPalettes(true);
}

void MainWindow::anchorFloatingPalettes(bool force)
{
    // Panels are children of the window, so they only need positioning when the
    // canvas rect actually changes. Re-pinning on every event made them twitch
    // while the window was dragged.
    if (!m_canvas || m_panels.isEmpty())
        return;
    // Never placed yet: do it even if the window happens to be moving, otherwise
    // the first move event would leave the panels stacked at the origin.
    const bool firstPlacement = m_lastCanvasRect.isNull();
    if (m_windowMoving && !firstPlacement)
        return;
    const QRect local(m_canvas->mapTo(this, QPoint(0, 0)), m_canvas->size());
    // Before the layout settles the canvas is a strip, and clamping a panel into
    // it collapses every panel onto the same point. Wait for a real area.
    if (local.width() < 200 || local.height() < 200)
        return;
    // Bail out only when the canvas did not move *and* every panel is already
    // where it should be; otherwise a re-pinned panel would stay where it was
    // dropped until the window happened to change size.
    bool allPinned = true;
    for (PalettePanel* p : m_panels)
        if (!p->isHidden() && !p->isPinned())
            allPinned = false;
    if (!force && local == m_lastCanvasRect && allPinned)
        return;
    m_lastCanvasRect = local;

    const int gap = 6;
    auto place = [&](PalettePanel* p, int horizontal, int vertical) {
        // isHidden() is the right test: it is false for a panel the user has
        // not closed, even while the window is still being mapped.
        if (!p || p->isHidden())
            return;
        if (p->isPinned()) {
            p->pinToCorner(horizontal, vertical, local);
            return;
        }
        // The user dragged this one: keep it exactly where they put it, only
        // pulled back inside the canvas if the window shrank.
        p->moveInside(p->savedPos(), local);
    };
    place(m_panelHistory, 1, 2);
    place(m_panelLayers, 1, 3);
    place(m_panelColors, 0, 3);

    for (PalettePanel* p : m_panels)
        if (!p->isHidden())
            p->raise();
}

void MainWindow::connectPanes()
{
    // The panes are built after the first document, so bind them now.
    if (m_layers)
        m_layers->setDocument(m_doc);
    if (m_history)
        m_history->setHistory(m_doc->history());
    if (m_status)
        m_status->setDocument(m_doc);
    if (m_properties)
        m_properties->setDocument(m_doc);

    m_canvas->setToolManager(m_tools);
    connect(m_tools, &ToolManager::activeToolChanged, this, &MainWindow::onActiveToolChanged);
    connect(m_canvas, &CanvasView::cursorMoved, this, &MainWindow::onCursorMoved);
    connect(m_canvas, &CanvasView::canvasResized, this, &MainWindow::onCanvasResized);
    connect(m_canvas, &CanvasView::zoomChanged, this, [this](double z) {
        m_status->setZoom(z);
        m_properties->setZoom(z);
    });

    connect(m_colors, &ColorPane::colorsChanged, this,
            [this](pixel_t p, pixel_t s) { m_tools->setPrimaryColor(p); m_tools->setSecondaryColor(s); });
    connect(m_brushes, &BrushesPane::brushChanged, this, [this](const Brush& b) {
        m_tools->setBrush(b);
        if (m_canvas->activeTool())
            m_canvas->activeTool()->setBrush(b);
        m_canvas->update();
    });
    connect(m_brushes, &BrushesPane::shapeChanged, this, [this](BrushShape) { m_canvas->update(); });

    connect(m_layers, &LayersPane::selectionChanged, this, [this](int index) {
        if (m_doc && index >= 0 && index < m_doc->layerCount())
            m_doc->setActiveLayerIndex(index);
    });
    connect(m_layers, &LayersPane::activeLayerChanged, this, [this](int index) {
        if (m_doc && index != m_doc->activeLayerIndex())
            m_doc->setActiveLayerIndex(index);
    });
    connect(m_layers, &LayersPane::layerPropertiesRequested, this, [this](int) {
        if (m_layers)
            m_layers->showLayerProperties();
    });
    connect(m_layers, &LayersPane::layerMenuRequested, this,
            [this](const QPoint& globalPos, int index) {
                if (m_layers && index >= 0 && m_doc && index < m_doc->layerCount())
                    m_doc->setActiveLayerIndex(index);
                if (m_layers && m_layers->findChild<QMenu*>())
                    Q_UNUSED(globalPos);
                QMenu menu(this);
                const QVector<const char*> ids = {
                    "layers.add",     "layers.delete",  "layers.duplicate", "layers.properties",
                    "layers.mergeDown", "layers.mergeVisible", "layers.flatten",
                    "layers.moveUp",  "layers.moveDown", "layers.rotate",   "layers.flipH",
                    "layers.flipV"
                };
                for (const char* id : ids)
                    if (QAction* a = m_actions.value(QString::fromLatin1(id)))
                        menu.addAction(a);
                menu.exec(globalPos);
            });
    connect(m_layers, &LayersPane::statusMessage, this,
            [this](const QString& t) { status(t); });
    connect(m_layers, &LayersPane::layerOrderChanged, this, [this](int from, int to) {
        if (m_layers)
            m_layers->requestMoveLayer(from, to);
    });

    connect(m_history, &HistoryPane::jumpRequested, this, &MainWindow::onHistoryJump);
    connect(m_gradients, &GradientPane::gradientChanged, this, [this](const Gradient& g) {
        m_canvas->setActiveGradient(&g);
    });
    connect(m_gradients, &GradientPane::statusMessage, this,
            [this](const QString& t) { status(t); });

    connect(m_properties, &PropertyPane::canvasSizeRequested, this, &MainWindow::resizeCanvas);
    connect(m_properties, &PropertyPane::resizeImageRequested, this, [this](int, int) {
        resizeImage();
    });
    connect(m_properties, &PropertyPane::zoomToFitRequested, this, [this] { m_canvas->zoomToFit(); });
    connect(m_properties, &PropertyPane::statusMessage, this,
            [this](const QString& t) { status(t); });
    connect(m_properties, &PropertyPane::selectionModeChanged, this, [this](int mode) {
        if (m_tools)
            m_tools->setActiveById(mode == 0   ? QStringLiteral("selectrect")
                                      : mode == 1 ? QStringLiteral("selectrect")
                                      : mode == 2 ? QStringLiteral("selectrect")
                                                  : QStringLiteral("selectrect"));
        status(tr("Selection mode: %1")
                   .arg(m_properties->findChildren<QComboBox*>().isEmpty()
                            ? QString()
                            : m_properties->findChildren<QComboBox*>().first()->currentText()));
    });

    connect(m_status, &StatusPane::zoomChanged, this, [this](double z) {
        if (z < 0)
            m_canvas->zoomToFit();
        else
            m_canvas->setZoom(z);
    });
    connect(m_status, &StatusPane::toolOptionsToggled, this, [this](bool on) {
        m_optionsBar->setVisible(on);
    });

    connect(m_optionsBar, &ToolOptionsBar::resetRequested, this, &MainWindow::onToolOptionsChanged);
    connect(m_optionsBar, &ToolOptionsBar::finishRequested, this, [this] {
        if (Tool* t = m_canvas ? m_canvas->activeTool() : nullptr)
            t->finishSession();
    });

    m_canvas->setActiveGradient(&m_gradients->gradient());
}

void MainWindow::buildShortcuts()
{
    m_toolShortcuts = ShortcutDisplayWidget::defaultToolShortcuts();
    m_commandShortcuts = {
        { QStringLiteral("Ctrl+N"), QStringLiteral("New") },
        { QStringLiteral("Ctrl+O"), QStringLiteral("Open") },
        { QStringLiteral("Ctrl+S"), QStringLiteral("Save") },
        { QStringLiteral("Ctrl+Z"), QStringLiteral("Undo") },
        { QStringLiteral("Ctrl+Y"), QStringLiteral("Redo") },
        { QStringLiteral("Ctrl+A"), QStringLiteral("Select All") },
        { QStringLiteral("Ctrl+C"), QStringLiteral("Copy") },
        { QStringLiteral("Ctrl+V"), QStringLiteral("Paste") },
        { QStringLiteral("Ctrl+X"), QStringLiteral("Cut") },
        { QStringLiteral("Delete"), QStringLiteral("Clear") },
        { QStringLiteral("Ctrl+1"), QStringLiteral("Zoom 100%") },
        { QStringLiteral("Ctrl+0"), QStringLiteral("Fit On Screen") },
    };

    // Map the tool shortcuts onto the tool actions.
    for (Tool* t : m_tools->tools()) {
        QAction* a = m_actions[QStringLiteral("tool.") + t->id()];
        if (!a)
            continue;
        for (auto it = m_toolShortcuts.constBegin(); it != m_toolShortcuts.constEnd(); ++it) {
            if (it.value().compare(t->name(), Qt::CaseInsensitive) == 0
                || it.value().compare(t->toolTip(), Qt::CaseInsensitive) == 0) {
                a->setShortcut(QKeySequence(it.key()));
                a->setShortcutContext(Qt::WindowShortcut);
                break;
            }
        }
    }
}

// ------------------------------------------------------------------ open/save

bool MainWindow::openFile(const QString& path)
{
    if (!m_doc) {
        openInNewWindow(path);
        return false;
    }
    const QString ext = QFileInfo(path).suffix().toLower();
    if (ext == QLatin1String("pdq") || ext == QLatin1String("pdn")) {
        QString err;
        // Two project formats share the File menu. .pdq is ours; .pdn is Paint.NET's,
        // which is a different format entirely and is read, never written.
        Document* doc = ext == QLatin1String("pdq") ? PdqFile::load(path, &err)
                                                     : Pdn3Reader::load(path, &err);
        if (!doc) {
            QMessageBox::warning(this, tr("Open"), err);
            return false;
        }
        // Replace the current document.
        Document* old = m_doc;
        m_doc = doc;
        m_doc->setParent(this);
        doc->setMaxHistoryLength(20);
        doc->history()->clear();
        doc->setFilePath(path);
        doc->setDirty(false);
        m_tools->setDocument(doc);
        m_canvas->setDocument(doc);
        m_canvas->zoomToFit();
        if (m_layers)
            m_layers->setDocument(doc);
        if (m_history)
            m_history->setHistory(doc->history());
        if (m_status)
            m_status->setDocument(doc);
        if (m_properties)
            m_properties->setDocument(doc);
        connect(doc, &Document::dirtyChanged, this, &MainWindow::onDocumentDirtyChanged);
        connect(doc, &Document::historyChanged, this, &MainWindow::updateActionStates);
        connect(doc, &Document::layersChanged, this, &MainWindow::updateActionStates);
        connect(doc, &Document::activeLayerChanged, this,
                [this](int) { updateActionStates(); });
        connect(doc, &Document::selectionChanged, this, [this] { updateActionStates(); });
        connect(doc->history(), &History::changed, this, &MainWindow::updateActionStates);
        old->deleteLater();
        m_recent->add(path);
        m_lastDir = QFileInfo(path).absolutePath();
        refreshWindowTitle();
        updateActionStates();
        status(tr("Opened %1").arg(QFileInfo(path).fileName()));
        return true;
    }

    QString err;
    QImage img = FileFormats::load(path, &err);
    if (img.isNull()) {
        QMessageBox::warning(this, tr("Open"),
                             err.isEmpty() ? tr("Could not open the file.") : err);
        return false;
    }
    m_doc->resizeCanvas(img.width(), img.height());
    for (int i = m_doc->layerCount() - 1; i > 0; --i)
        m_doc->removeLayerAt(i);
    Layer* layer = m_doc->layerAt(0);
    layer->setSurface(Surface::fromFormat(img));
    layer->setName(tr("Background"));
    layer->setBackground(true);
    layer->markThumbnailDirty();
    m_doc->setFilePath(path);
    m_doc->setDirty(false);
    m_doc->deselect();
    m_canvas->invalidateTiles(QRect());
    m_canvas->zoomToFit();
    m_fitted = true;
    if (m_layers)
        m_layers->refresh();
    m_recent->add(path);
    m_lastDir = QFileInfo(path).absolutePath();
    refreshWindowTitle();
    updateActionStates();
    status(tr("Opened %1 (%2)").arg(QFileInfo(path).fileName(), FileFormats::humanFileSize(path)));
    return true;
}

void MainWindow::openInNewWindow(const QString& path)
{
    MainWindow* w = new MainWindow;
    w->show();
    w->openFile(path);
}

void MainWindow::newImage()
{
    if (m_doc && m_doc->isDirty()) {
        const auto r = QMessageBox::question(
            this, tr("Paint.QT"),
            tr("The current image has unsaved changes.\nDo you want to save them?"),
            QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
        if (r == QMessageBox::Cancel)
            return;
        if (r == QMessageBox::Save && !save())
            return;
    }
    NewImageDialog dlg(this);
    if (dlg.exec() != QDialog::Accepted)
        return;
    createDocument(dlg.imageWidth(), dlg.imageHeight(), dlg.singleLayerBackground(),
                   dlg.backgroundColor());
    m_canvas->zoomToFit();
    m_fitted = true;
    status(tr("New image created"));
}

bool MainWindow::save()
{
    if (!m_doc)
        return false;
    if (m_doc->filePath().isEmpty()
        || QFileInfo(m_doc->filePath()).suffix().toLower() != QLatin1String("pdq"))
        return saveAs();
    return saveAs(); // Everything is always written through saveAs to keep the format honest.
}

bool MainWindow::saveAs()
{
    if (!m_doc)
        return false;
    QString filter;
    const QStringList filters = FileFormats::saveFilters();
    QString path = QFileDialog::getSaveFileName(
        this, tr("Save As"), m_lastDir, filters.join(QLatin1Char('\n')), &filter);
    if (path.isEmpty())
        return false;
    // Make sure the extension matches the chosen filter.
    const QString ext = QFileInfo(path).suffix().toLower();
    if (ext.isEmpty()) {
        // Derive the extension from the filter.
        const int dot = filter.lastIndexOf(QLatin1String("(*."));
        if (dot >= 0) {
            const int close = filter.indexOf(QLatin1Char(')'), dot);
            const QString exts = filter.mid(dot + 3, close - dot - 3);
            path += QLatin1Char('.') + exts.split(QLatin1Char(' ')).first();
        }
    }
    m_lastDir = QFileInfo(path).absolutePath();

    if (QFileInfo(path).suffix().toLower() == QLatin1String("pdq")) {
        QString err;
        if (!PdqFile::save(*m_doc, path, &err)) {
            QMessageBox::warning(this, tr("Save"), err);
            return false;
        }
        m_doc->setFilePath(path);
        m_doc->markSaved();
        m_recent->add(path);
        refreshWindowTitle();
        status(tr("Saved %1 (%2)").arg(QFileInfo(path).fileName(), FileFormats::humanFileSize(path)));
        return true;
    }

    // Raster export of the flattened composite.
    QSettings settings;
    SaveOptions opts;
    opts.jpegQuality = settings.value(QStringLiteral("files/jpegQuality"), 90).toInt();
    const bool flatten = !FileFormats::supportsTransparency(path)
                         && settings.value(QStringLiteral("files/warnFlatten"), true).toBool();
    if (flatten) {
        const auto r = QMessageBox::question(
            this, tr("Save"),
            tr("The chosen format does not support transparency.\n"
               "The image will be flattened onto a white background. Continue?"),
            QMessageBox::Yes | QMessageBox::No);
        if (r == QMessageBox::No)
            return false;
    }
    const QImage img = FileFormats::exportImage(*m_doc, flatten);
    QString err;
    if (!FileFormats::saveImage(img, path, opts, &err)) {
        QMessageBox::warning(this, tr("Save"), err);
        return false;
    }
    m_doc->setFilePath(path);
    m_doc->markSaved();
    m_recent->add(path);
    refreshWindowTitle();
    status(tr("Saved %1 (%2)").arg(QFileInfo(path).fileName(), FileFormats::humanFileSize(path)));
    return true;
}

bool MainWindow::exportAs()
{
    if (!m_doc)
        return false;
    const QRect r = m_doc->hasSelection() ? m_doc->selectionBounds() : m_doc->bounds();
    QImage img = m_doc->compositeImage();
    if (m_doc->hasSelection())
        img = ImageOps::compositeSelected(*m_doc).toQImage();
    Q_UNUSED(r);
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Export To File"), m_lastDir, FileFormats::saveFilters().join(QLatin1Char('\n')));
    if (path.isEmpty())
        return false;
    SaveOptions opts;
    opts.jpegQuality = QSettings().value(QStringLiteral("files/jpegQuality"), 90).toInt();
    QString err;
    if (!FileFormats::saveImage(img, path, opts, &err)) {
        QMessageBox::warning(this, tr("Export"), err);
        return false;
    }
    m_lastDir = QFileInfo(path).absolutePath();
    status(tr("Exported to %1 (%2)").arg(QFileInfo(path).fileName(), FileFormats::humanFileSize(path)));
    return true;
}

bool MainWindow::importFromFile()
{
    if (!m_doc)
        return false;
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Import From File"), m_lastDir, FileFormats::importFilters().join(QLatin1Char('\n')));
    if (path.isEmpty())
        return false;
    QImage img = FileFormats::load(path);
    if (img.isNull()) {
        QMessageBox::warning(this, tr("Import"), tr("Could not import the file."));
        return false;
    }
    m_lastDir = QFileInfo(path).absolutePath();
    Layer* layer = m_doc->addLayer(QFileInfo(path).fileName());
    Surface s(m_doc->width(), m_doc->height());
    s.copyFrom(Surface::fromFormat(img), QPoint(0, 0), img.rect());
    layer->setSurface(s);
    layer->markThumbnailDirty();
    m_doc->notifyLayerStructure();
    status(tr("Imported %1 as a new layer").arg(QFileInfo(path).fileName()));
    return true;
}

bool MainWindow::saveSelection()
{
    if (!m_doc || !m_doc->hasSelection()) {
        status(tr("There is no selection to save"));
        return false;
    }
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Save Selection"), m_lastDir, tr("Selection files (*.pdq)"));
    if (path.isEmpty())
        return false;
    QString err;
    if (!PdqFile::saveSelection(m_doc->selection(), path, &err)) {
        QMessageBox::warning(this, tr("Save Selection"), err);
        return false;
    }
    status(tr("Selection saved"));
    return true;
}

bool MainWindow::loadSelection()
{
    if (!m_doc)
        return false;
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Load Selection"), m_lastDir, tr("Selection files (*.pdq)"));
    if (path.isEmpty())
        return false;
    Selection sel;
    QString err;
    if (!PdqFile::loadSelection(path, &sel, &err)) {
        QMessageBox::warning(this, tr("Load Selection"), err);
        return false;
    }
    m_doc->setSelection(sel);
    status(tr("Selection loaded"));
    return true;
}

void MainWindow::print()
{
#ifdef PNQ_PRINT_SUPPORT
    if (!m_doc)
        return;
    QPrinter printer(QPrinter::HighResolution);
    QPrintDialog dlg(&printer, this);
    dlg.setWindowTitle(tr("Print"));
    if (dlg.exec() != QDialog::Accepted)
        return;
    QPainter p(&printer);
    const QImage img = FileFormats::exportImage(*m_doc, true);
    const QRectF target(printer.pageRect(QPrinter::DevicePixel));
    p.drawImage(target, img, QRectF(img.rect()));
    status(tr("Sent to printer"));
#else
    QMessageBox::information(this, tr("Print"),
                             tr("Printing support is not available in this build."));
#endif
}

// ------------------------------------------------------------------ edit

void MainWindow::undo()
{
    if (m_doc && m_doc->history()->canUndo()) {
        m_doc->history()->undo();
        status(tr("Undo: %1").arg(m_doc->history()->redoName()));
    }
}

void MainWindow::redo()
{
    if (m_doc && m_doc->history()->canRedo()) {
        m_doc->history()->redo();
        status(tr("Redo: %1").arg(m_doc->history()->undoName()));
    }
}

void MainWindow::setClipboardPixels(const Surface& s)
{
    m_clipboard = s;
    // Publish to the system clipboard as well, so the pixels can be pasted into
    // any other program.
    const QImage img = s.toQImage();
    if (!img.isNull() && m_clip)
        m_clip->setImage(img);
    updateActionStates();
}

bool MainWindow::importClipboardImage()
{
    if (!m_clip)
        return false;
    const QImage image = m_clip->currentImage();
    if (image.isNull())
        return false;
    m_clipboard = Surface::fromQImage(image);
    return !m_clipboard.isNull();
}

void MainWindow::copy()
{
    if (!m_doc)
        return;
    Surface px;
    ImageOps::copySelection(*m_doc, &px, false);
    setClipboardPixels(px);
    status(tr("Copied to clipboard"));
}

void MainWindow::copyMerged()
{
    if (!m_doc)
        return;
    Surface px;
    ImageOps::copySelection(*m_doc, &px, true);
    setClipboardPixels(px);
    status(tr("Copied merged to clipboard"));
}

void MainWindow::cut()
{
    copy();
    clearSelection();
    status(tr("Cut to clipboard"));
}

void MainWindow::paste()
{
    if (!m_doc)
        return;
    // The system clipboard is the source of truth: an image copied in another
    // program, or one we copied before this application was restarted, both
    // land here. The bridge only reports *changes*; the pixels are re-read here
    // so nothing can go stale.
    importClipboardImage();
    if (m_clipboard.isNull())
        return;
    const QPoint at = m_canvas->lastCursorImagePos();
    const int x = (at.x() < 0) ? m_pasteOffset.x() : at.x() - m_clipboard.width() / 2;
    const int y = (at.y() < 0) ? m_pasteOffset.y() : at.y() - m_clipboard.height() / 2;
    ImageOps::pasteIntoLayer(*m_doc, m_clipboard, qMax(0, x), qMax(0, y), m_doc->activeLayerIndex());
    m_pasteOffset = QPoint(qMax(0, x), qMax(0, y));
}

void MainWindow::pasteIntoNewLayer()
{
    if (!m_doc)
        return;
    importClipboardImage();
    if (m_clipboard.isNull())
        return;
    Layer* l = ImageOps::pasteAsNewLayer(*m_doc, m_clipboard, 0, 0, tr("Pasted Layer"));
    if (l)
        m_doc->setActiveLayerIndex(m_doc->activeLayerIndex());
    status(tr("Pasted into a new layer"));
}

void MainWindow::clearSelection()
{
    if (!m_doc)
        return;
    const bool hadSelection = m_doc->hasSelection();
    ImageOps::clearSelection(*m_doc);
    status(hadSelection ? tr("Selection cleared") : tr("Image cleared"));
}

void MainWindow::deleteSelection()
{
    clearSelection();
    m_doc->deselect();
}

void MainWindow::selectAll()
{
    if (m_doc)
        m_doc->selectAll();
}

void MainWindow::deselectAll()
{
    if (m_doc)
        m_doc->deselect();
}

void MainWindow::invertSelection()
{
    if (m_doc)
        m_doc->invertSelection();
}

// ------------------------------------------------------------------ image ops

void MainWindow::resizeImage()
{
    if (!m_doc)
        return;
    ResizeDialog dlg(this, m_doc->width(), m_doc->height(), false);
    if (dlg.exec() != QDialog::Accepted)
        return;
    static const ImageOps::ResizeMode modes[] = {
        ImageOps::ResizeMode::Normal,      ImageOps::ResizeMode::PreserveAspect,
        ImageOps::ResizeMode::DoNotPreserveAspect, ImageOps::ResizeMode::Crop,
        ImageOps::ResizeMode::Pad,         ImageOps::ResizeMode::PadCenter,
        ImageOps::ResizeMode::PadTopLeft
    };
    const int idx = qBound(0, dlg.modeIndex(), 6);
    const bool warn = QSettings().value(QStringLiteral("files/warnResize"), true).toBool();
    if (warn) {
        const auto r = QMessageBox::question(
            this, tr("Resize Image"),
            tr("Resize the image to %1 x %2?\nThis scales every layer and cannot be undone as a "
               "simple step, but it is added to the undo history.")
                .arg(dlg.width())
                .arg(dlg.height()),
            QMessageBox::Yes | QMessageBox::No);
        if (r == QMessageBox::No)
            return;
    }
    ImageOps::resizeDocument(*m_doc, dlg.width(), dlg.height(), dlg.smooth(), modes[idx], true);
    status(tr("Resized to %1 x %2").arg(m_doc->width()).arg(m_doc->height()));
    if (m_properties)
        m_properties->setDocument(m_doc);
}

void MainWindow::resizeCanvas()
{
    if (!m_doc)
        return;
    CanvasSizeDialog dlg(this, m_doc->width(), m_doc->height());
    if (dlg.exec() != QDialog::Accepted)
        return;
    m_doc->resizeCanvas(dlg.width(), dlg.height(), dlg.anchor());
    if (m_properties)
        m_properties->setDocument(m_doc);
    m_canvas->zoomToFit();
    status(tr("Canvas resized to %1 x %2").arg(m_doc->width()).arg(m_doc->height()));
}

void MainWindow::rotateImage()
{
    if (!m_doc)
        return;
    bool ok = false;
    const double deg = QInputDialog::getDouble(this, tr("Rotate Image"), tr("Angle (degrees):"), 90.0,
                                               -360.0, 360.0, 1, &ok);
    if (!ok)
        return;
    rotateImage(deg);
}

void MainWindow::rotateImage(double degrees)
{
    if (!m_doc)
        return;
    ImageOps::rotateDocument(*m_doc, degrees, false);
    m_canvas->zoomToFit();
    if (m_properties)
        m_properties->setDocument(m_doc);
    status(tr("Rotated by %1°").arg(degrees));
}

void MainWindow::rotateImageLeft()
{
    if (!m_doc)
        return;
    ImageOps::rotateDocument(*m_doc, -90.0, false);
    m_canvas->zoomToFit();
    if (m_properties)
        m_properties->setDocument(m_doc);
    status(tr("Rotated 90° counter-clockwise"));
}

void MainWindow::rotateImageRight()
{
    if (!m_doc)
        return;
    ImageOps::rotateDocument(*m_doc, 90.0, false);
    m_canvas->zoomToFit();
    if (m_properties)
        m_properties->setDocument(m_doc);
    status(tr("Rotated 90° clockwise"));
}

void MainWindow::flipImageHorizontal()
{
    if (!m_doc)
        return;
    ImageOps::flipDocument(*m_doc, true);
    status(tr("Flipped horizontally"));
}

void MainWindow::flipImageVertical()
{
    if (!m_doc)
        return;
    ImageOps::flipDocument(*m_doc, false);
    status(tr("Flipped vertically"));
}

void MainWindow::rotateLayer()
{
    if (!m_doc)
        return;
    bool ok = false;
    const double deg = QInputDialog::getDouble(this, tr("Rotate Layer"), tr("Angle (degrees):"), 90.0,
                                               -360.0, 360.0, 1, &ok);
    if (!ok)
        return;
    ImageOps::rotateLayers(*m_doc, deg, false);
    status(tr("Layer rotated by %1°").arg(deg));
}

void MainWindow::flipLayerHorizontal()
{
    if (m_doc)
        ImageOps::flipLayers(*m_doc, true);
}

void MainWindow::flipLayerVertical()
{
    if (m_doc)
        ImageOps::flipLayers(*m_doc, false);
}

void MainWindow::cropToSelection()
{
    if (!m_doc)
        return;
    if (!m_doc->hasSelection()) {
        status(tr("There is no selection to crop to"));
        return;
    }
    m_doc->cropCanvas(m_doc->selectionBounds());
    if (m_properties)
        m_properties->setDocument(m_doc);
    m_canvas->zoomToFit();
    status(tr("Cropped to selection"));
}

void MainWindow::cropToLayer()
{
    if (!m_doc)
        return;
    // Bounding box of the non-transparent pixels of the active layer.
    Layer* l = m_doc->activeLayer();
    if (!l)
        return;
    Surface comp(l->width(), l->height());
    Renderer::compositeLayer(l, comp);
    QRect r;
    for (int y = 0; y < comp.height(); ++y) {
        const pixel_t* row = comp.scanLine(y);
        int minX = -1, maxX = -1;
        for (int x = 0; x < comp.width(); ++x) {
            if (getA(row[x]) != 0) {
                if (minX < 0)
                    minX = x;
                maxX = x;
            }
        }
        if (minX >= 0)
            r = r.isNull() ? QRect(minX, y, maxX - minX + 1, 1) : r.united(QRect(minX, y, maxX - minX + 1, 1));
    }
    if (r.isNull()) {
        status(tr("The layer is empty"));
        return;
    }
    m_doc->cropCanvas(r);
    if (m_properties)
        m_properties->setDocument(m_doc);
    m_canvas->zoomToFit();
    status(tr("Cropped to layer"));
}

void MainWindow::trim()
{
    if (!m_doc)
        return;
    m_doc->trimTransparent(false);
    if (m_properties)
        m_properties->setDocument(m_doc);
    m_canvas->zoomToFit();
    status(tr("Trimmed transparent edges"));
}

void MainWindow::centerLayer()
{
    if (!m_doc)
        return;
    m_doc->centerLayer();
    status(tr("Layer centered"));
}

void MainWindow::mergeVisible()
{
    if (!m_doc)
        return;
    m_doc->mergeVisible();
    if (m_layers)
        m_layers->refresh();
    status(tr("Merged visible layers"));
}

void MainWindow::flattenImage()
{
    if (!m_doc)
        return;
    if (QSettings().value(QStringLiteral("files/warnFlatten"), true).toBool()) {
        const auto r = QMessageBox::question(
            this, tr("Flatten Image"),
            tr("Flattening the image will discard transparency and all layers except one.\n"
               "Do you want to continue?"),
            QMessageBox::Yes | QMessageBox::No);
        if (r == QMessageBox::No)
            return;
    }
    m_doc->flattenImage();
    if (m_layers)
        m_layers->refresh();
    status(tr("Image flattened"));
}

void MainWindow::addLayer()
{
    if (m_layers)
        m_layers->addLayer();
}

// ------------------------------------------------------------------ adjustments

void MainWindow::applyLivePreview(const QString& name,
                                 const std::function<void(Surface&, const Selection&)>& fn)
{
    if (!m_doc)
        return;
    Layer* layer = m_doc->activeLayer();
    if (!layer)
        return;
    if (!m_livePreview || m_liveLayer != m_doc->activeLayerIndex()) {
        m_liveLayer = m_doc->activeLayerIndex();
        m_liveOriginal = layer->surface().copy();
        m_livePreview = true;
    }
    Surface work = m_liveOriginal.copy();
    fn(work, m_doc->selection());
    layer->setSurface(work);
    layer->markThumbnailDirty();
    m_doc->notifyLayerPixels(m_liveLayer, layer->bounds());
    Q_UNUSED(name);
}

void MainWindow::applyToActiveLayer(const QString& name,
                                   const std::function<void(Surface&, const Selection&)>& fn,
                                   bool preview)
{
    if (!m_doc)
        return;
    Layer* layer = m_doc->activeLayer();
    if (!layer)
        return;
    const int idx = m_doc->activeLayerIndex();
    if (preview) {
        applyLivePreview(name, fn);
        return;
    }
    if (m_livePreview) {
        m_livePreview = false;
        const Surface after = layer->surface().copy();
        m_doc->history()->push(new SurfaceAction(name, idx, m_liveOriginal, after));
        m_liveLayer = -1;
        return;
    }
    const Surface before = layer->surface().copy();
    Surface work = before.copy();
    fn(work, m_doc->selection());
    layer->setSurface(work);
    layer->markThumbnailDirty();
    m_doc->history()->push(new SurfaceAction(name, idx, before, work));
    m_doc->notifyLayerPixels(idx, layer->bounds());
}

void MainWindow::openAdjustments()
{
    if (!m_doc)
        return;
    // The menu bar already offers every adjustment, so just run the default one.
    openAdjustmentById(QStringLiteral("adj.brightness"));
}

void MainWindow::openAdjustmentById(const QString& id)
{
    if (!m_doc)
        return;
    if (id == QLatin1String("adj.brightness")) {
        BrightnessContrastDialog dlg(this);
        connect(&dlg, &BrightnessContrastDialog::previewRequested, this, [&] {
            const int b = dlg.brightness(), c = dlg.contrast();
            applyLivePreview(tr("Brightness/Contrast"), [&](Surface& s, const Selection& sel) {
                Effects::brightnessContrast(s, sel, b, c);
            });
        });
        if (dlg.exec() == QDialog::Accepted) {
            const int b = dlg.brightness(), c = dlg.contrast();
            m_livePreview = false;
            m_liveLayer = -1;
            applyToActiveLayer(tr("Brightness/Contrast"), [&](Surface& s, const Selection& sel) {
                Effects::brightnessContrast(s, sel, b, c);
            }, false);
        } else {
            m_livePreview = false;
        }
        updateActionStates();
        return;
    }

    // Map the menu action ids onto the internal adjustment identifiers.
    static const QHash<QString, QString> map = {
        { QStringLiteral("adj.brightness"), QStringLiteral("brightness") },
        { QStringLiteral("adj.curves"), QStringLiteral("curves") },
        { QStringLiteral("adj.levels"), QStringLiteral("levels") },
        { QStringLiteral("adj.huesat"), QStringLiteral("huesaturation") },
        { QStringLiteral("adj.colorbalance"), QStringLiteral("colorbalance") },
        { QStringLiteral("adj.desaturate"), QStringLiteral("desaturate") },
        { QStringLiteral("adj.posterize"), QStringLiteral("posterize") },
        { QStringLiteral("adj.threshold"), QStringLiteral("threshold") },
        { QStringLiteral("adj.selective"), QStringLiteral("selectivecolor") },
        { QStringLiteral("adj.temperature"), QStringLiteral("temperature") },
        { QStringLiteral("adj.replacecolor"), QStringLiteral("replacecolor") },
        { QStringLiteral("adj.invert"), QStringLiteral("invert") },
        { QStringLiteral("adj.autolevels"), QStringLiteral("autolevels") },
        { QStringLiteral("adj.channelmixer"), QStringLiteral("channelmixer") },
    };
    const QString adj = map.value(id, id);

    auto commit = [this](const QString& name,
                         const std::function<void(Surface&, const Selection&)>& fn) {
        m_livePreview = false;
        m_liveLayer = -1;
        applyToActiveLayer(name, fn, false);
    };

    if (adj == QLatin1String("brightness")) {
        BrightnessContrastDialog dlg(this);
        connect(&dlg, &BrightnessContrastDialog::previewRequested, this, [&] {
            const int b = dlg.brightness(), c = dlg.contrast();
            applyLivePreview(tr("Brightness/Contrast"), [&](Surface& s, const Selection& sel) {
                Effects::brightnessContrast(s, sel, b, c);
            });
        });
        if (dlg.exec() == QDialog::Accepted) {
            const int b = dlg.brightness(), c = dlg.contrast();
            commit(tr("Brightness/Contrast"), [&](Surface& s, const Selection& sel) {
                Effects::brightnessContrast(s, sel, b, c);
            });
        } else {
            m_livePreview = false;
        }
    } else if (adj == QLatin1String("huesaturation")) {
        HueSaturationDialog dlg(this);
        connect(&dlg, &HueSaturationDialog::previewRequested, this, [&] {
            const int h = dlg.hue(), sa = dlg.saturation(), li = dlg.lightness();
            const bool cz = dlg.colorize();
            applyLivePreview(tr("Hue/Saturation"), [&](Surface& s, const Selection& sel) {
                Effects::hueSaturation(s, sel, h, sa, li, cz);
            });
        });
        if (dlg.exec() == QDialog::Accepted) {
            const int h = dlg.hue(), sa = dlg.saturation(), li = dlg.lightness();
            const bool cz = dlg.colorize();
            commit(tr("Hue/Saturation"),
                   [&](Surface& s, const Selection& sel) { Effects::hueSaturation(s, sel, h, sa, li, cz); });
        } else {
            m_livePreview = false;
        }
    } else if (adj == QLatin1String("curves")) {
        CurvesDialog dlg(this);
        connect(&dlg, &CurvesDialog::previewRequested, this, [&] {
            const QVector<int> rgb = dlg.points(0);
            const QVector<int> r = dlg.points(1);
            const QVector<int> g = dlg.points(2);
            const QVector<int> b = dlg.points(3);
            applyLivePreview(tr("Curves"), [&](Surface& s, const Selection& sel) {
                Effects::curves(s, sel, rgb, r, g, b);
            });
        });
        if (dlg.exec() == QDialog::Accepted) {
            const QVector<int> rgb = dlg.points(0), r = dlg.points(1), g = dlg.points(2),
                                b = dlg.points(3);
            commit(tr("Curves"),
                  [&](Surface& s, const Selection& sel) { Effects::curves(s, sel, rgb, r, g, b); });
        } else {
            m_livePreview = false;
        }
    } else if (adj == QLatin1String("levels")) {
        LevelsDialog dlg(this);
        connect(&dlg, &LevelsDialog::previewRequested, this, [&] {
            const int il = dlg.inputLow(), ih = dlg.inputHigh();
            const int ol = dlg.outputLow(), oh = dlg.outputHigh();
            const double g = dlg.gamma();
            const bool per = dlg.usePerChannel();
            const int rl = dlg.channelLow(0), rh = dlg.channelHigh(0);
            const double rg = dlg.channelGamma(0);
            const int gl = dlg.channelLow(1), gh = dlg.channelHigh(1);
            const double gg = dlg.channelGamma(1);
            const int bl = dlg.channelLow(2), bh = dlg.channelHigh(2);
            const double bg = dlg.channelGamma(2);
            applyLivePreview(tr("Levels"), [&](Surface& s, const Selection& sel) {
                Effects::levels(s, sel, il, ih, ol, oh, g, per, rl, rh, rg, gl, gh, gg, bl, bh, bg);
            });
        });
        if (dlg.exec() == QDialog::Accepted) {
            const int il = dlg.inputLow(), ih = dlg.inputHigh();
            const int ol = dlg.outputLow(), oh = dlg.outputHigh();
            const double g = dlg.gamma();
            const bool per = dlg.usePerChannel();
            const int rl = dlg.channelLow(0), rh = dlg.channelHigh(0);
            const double rg = dlg.channelGamma(0);
            const int gl = dlg.channelLow(1), gh = dlg.channelHigh(1);
            const double gg = dlg.channelGamma(1);
            const int bl = dlg.channelLow(2), bh = dlg.channelHigh(2);
            const double bg = dlg.channelGamma(2);
            commit(tr("Levels"), [&](Surface& s, const Selection& sel) {
                Effects::levels(s, sel, il, ih, ol, oh, g, per, rl, rh, rg, gl, gh, gg, bl, bh, bg);
            });
        } else {
            m_livePreview = false;
        }
    } else if (adj == QLatin1String("invert")) {
        commit(tr("Invert"), [&](Surface& s, const Selection& sel) { Effects::invert(s, sel); });
    } else if (adj == QLatin1String("autolevels")) {
        commit(tr("Auto-Adjust Levels"),
               [&](Surface& s, const Selection& sel) { Effects::autoAdjustLevels(s, sel); });
    } else if (adj == QLatin1String("channelmixer")) {
        ChannelMixerDialog dlg(this);
        connect(&dlg, &ChannelMixerDialog::previewRequested, this, [&] {
            int m[3][3];
            for (int r = 0; r < 3; ++r)
                for (int c = 0; c < 3; ++c)
                    m[r][c] = dlg.matrix(r, c);
            const bool mono = dlg.monochrome();
            applyLivePreview(tr("Channel Mixer"), [&](Surface& s, const Selection& sel) {
                Effects::channelMixer(s, sel, m[0][0], m[0][1], m[0][2], m[1][0], m[1][1], m[1][2],
                                      m[2][0], m[2][1], m[2][2], mono, m[0][0], m[1][1], m[2][2]);
            });
        });
        if (dlg.exec() == QDialog::Accepted) {
            int m[3][3];
            for (int r = 0; r < 3; ++r)
                for (int c = 0; c < 3; ++c)
                    m[r][c] = dlg.matrix(r, c);
            const bool mono = dlg.monochrome();
            commit(tr("Channel Mixer"), [&](Surface& s, const Selection& sel) {
                Effects::channelMixer(s, sel, m[0][0], m[0][1], m[0][2], m[1][0], m[1][1], m[1][2],
                                      m[2][0], m[2][1], m[2][2], mono, m[0][0], m[1][1], m[2][2]);
            });
        } else {
            m_livePreview = false;
        }
    } else if (adj == QLatin1String("selectivecolor")) {
        SelectiveColorDialog dlg(this);
        connect(&dlg, &SelectiveColorDialog::previewRequested, this, [&] {
            int v[7];
            for (int i = 0; i < 7; ++i)
                v[i] = dlg.value(i);
            const bool rel = dlg.relative();
            applyLivePreview(tr("Selective Color"), [&](Surface& s, const Selection& sel) {
                Effects::selectiveColor(s, sel, v[0], v[1], v[2], v[3], v[4], v[5], v[6], rel, 0, 0, 0,
                                        0);
            });
        });
        if (dlg.exec() == QDialog::Accepted) {
            int v[7];
            for (int i = 0; i < 7; ++i)
                v[i] = dlg.value(i);
            const bool rel = dlg.relative();
            commit(tr("Selective Color"), [&](Surface& s, const Selection& sel) {
                Effects::selectiveColor(s, sel, v[0], v[1], v[2], v[3], v[4], v[5], v[6], rel, 0, 0, 0,
                                        0);
            });
        } else {
            m_livePreview = false;
        }
    } else {
        // The remaining simple adjustments share one dialog.
        SimpleAdjustmentDialog::Kind kind = SimpleAdjustmentDialog::Kind::Invert;
        if (adj == QLatin1String("posterize"))
            kind = SimpleAdjustmentDialog::Kind::Posterize;
        else if (adj == QLatin1String("threshold"))
            kind = SimpleAdjustmentDialog::Kind::Threshold;
        else if (adj == QLatin1String("desaturate"))
            kind = SimpleAdjustmentDialog::Kind::Desaturate;
        else if (adj == QLatin1String("colorbalance"))
            kind = SimpleAdjustmentDialog::Kind::ColorBalance;
        else if (adj == QLatin1String("temperature"))
            kind = SimpleAdjustmentDialog::Kind::TemperatureTint;
        else if (adj == QLatin1String("replacecolor"))
            kind = SimpleAdjustmentDialog::Kind::ReplaceColor;
        else if (adj == QLatin1String("invert"))
            kind = SimpleAdjustmentDialog::Kind::Invert;

        SimpleAdjustmentDialog dlg(kind, this);
        connect(&dlg, &SimpleAdjustmentDialog::previewRequested, this, [&] {
            const QVector<int> v = dlg.values();
            const bool flag = dlg.flag();
            applyLivePreview(dlg.windowTitle(), [&](Surface& s, const Selection& sel) {
                switch (kind) {
                case SimpleAdjustmentDialog::Kind::Posterize:
                    Effects::posterize(s, sel, v.value(0, 4), v.value(1, 0));
                    break;
                case SimpleAdjustmentDialog::Kind::Threshold:
                    Effects::threshold(s, sel, v.value(0, 128));
                    break;
                case SimpleAdjustmentDialog::Kind::Desaturate:
                    Effects::desaturate(s, sel, v.value(0, 50), true, v.value(1, 45),
                                        v.value(2, 40), v.value(3, 15), false);
                    break;
                case SimpleAdjustmentDialog::Kind::ColorBalance:
                    Effects::colorBalance(s, sel, v.value(0), v.value(1), v.value(2), v.value(3),
                                          v.value(4), v.value(5), v.value(6), v.value(7), v.value(8),
                                          false);
                    break;
                case SimpleAdjustmentDialog::Kind::TemperatureTint:
                    Effects::temperatureTint(s, sel, v.value(0), v.value(1));
                    break;
                case SimpleAdjustmentDialog::Kind::ReplaceColor:
                    Effects::replaceColor(s, sel, m_colors->primary(), m_colors->primary(),
                                          v.value(0), flag, v.value(1), v.value(2), v.value(3));
                    break;
                case SimpleAdjustmentDialog::Kind::Invert:
                    Effects::invert(s, sel);
                    break;
                }
            });
        });
        if (dlg.exec() == QDialog::Accepted) {
            const QVector<int> v = dlg.values();
            const bool flag = dlg.flag();
            commit(dlg.windowTitle(), [&](Surface& s, const Selection& sel) {
                switch (kind) {
                case SimpleAdjustmentDialog::Kind::Posterize:
                    Effects::posterize(s, sel, v.value(0, 4), v.value(1, 0));
                    break;
                case SimpleAdjustmentDialog::Kind::Threshold:
                    Effects::threshold(s, sel, v.value(0, 128));
                    break;
                case SimpleAdjustmentDialog::Kind::Desaturate:
                    Effects::desaturate(s, sel, v.value(0, 50), true, v.value(1, 45), v.value(2, 40),
                                        v.value(3, 15), false);
                    break;
                case SimpleAdjustmentDialog::Kind::ColorBalance:
                    Effects::colorBalance(s, sel, v.value(0), v.value(1), v.value(2), v.value(3),
                                          v.value(4), v.value(5), v.value(6), v.value(7), v.value(8),
                                          false);
                    break;
                case SimpleAdjustmentDialog::Kind::TemperatureTint:
                    Effects::temperatureTint(s, sel, v.value(0), v.value(1));
                    break;
                case SimpleAdjustmentDialog::Kind::ReplaceColor:
                    Effects::replaceColor(s, sel, m_colors->primary(), m_colors->primary(),
                                          v.value(0), flag, v.value(1), v.value(2), v.value(3));
                    break;
                case SimpleAdjustmentDialog::Kind::Invert:
                    Effects::invert(s, sel);
                    break;
                }
            });
        } else {
            m_livePreview = false;
        }
    }
    updateActionStates();
}

void MainWindow::openEffects()
{
    if (!m_doc)
        return;
    QMenu menu(this);
    QVector<QAction*> actions;
    auto entry = [&](const QString& text, const QString& id, const QString& category) {
        QAction* a = menu.addAction(text);
        a->setData(id + QLatin1Char('|') + category);
        actions.append(a);
    };
    // Blur
    entry(tr("Gaussian Blur..."), QStringLiteral("gauss"), QStringLiteral("blur"));
    entry(tr("Box Blur..."), QStringLiteral("boxblur"), QStringLiteral("blur"));
    entry(tr("Motion Blur..."), QStringLiteral("motion"), QStringLiteral("blur"));
    entry(tr("Zoom Blur..."), QStringLiteral("zoomblur"), QStringLiteral("blur"));
    entry(tr("Radial Blur..."), QStringLiteral("radialblur"), QStringLiteral("blur"));
    entry(tr("Surface Blur..."), QStringLiteral("surfaceblur"), QStringLiteral("blur"));
    entry(tr("Sharpen..."), QStringLiteral("sharpen"), QStringLiteral("blur"));
    entry(tr("Glow..."), QStringLiteral("glow"), QStringLiteral("blur"));
    entry(tr("Drop Shadow..."), QStringLiteral("dropshadow"), QStringLiteral("blur"));
    entry(tr("Inner Shadow..."), QStringLiteral("innershadow"), QStringLiteral("blur"));
    entry(tr("Unsharp Mask..."), QStringLiteral("unsharp"), QStringLiteral("blur"));
    // Stylize
    entry(tr("Emboss..."), QStringLiteral("emboss"), QStringLiteral("stylize"));
    entry(tr("Invert Emboss..."), QStringLiteral("invertemboss"), QStringLiteral("stylize"));
    entry(tr("Edge Detect..."), QStringLiteral("edgedetect"), QStringLiteral("stylize"));
    entry(tr("Pixelate..."), QStringLiteral("pixelate"), QStringLiteral("stylize"));
    entry(tr("Mosaic..."), QStringLiteral("mosaic"), QStringLiteral("stylize"));
    entry(tr("Oil..."), QStringLiteral("oilpaint"), QStringLiteral("stylize"));
    entry(tr("Cel Shading..."), QStringLiteral("celshading"), QStringLiteral("stylize"));
    entry(tr("Vignette..."), QStringLiteral("vignette"), QStringLiteral("stylize"));
    entry(tr("Old Photo..."), QStringLiteral("oldphoto"), QStringLiteral("stylize"));
    entry(tr("Crystalize..."), QStringLiteral("crystalize"), QStringLiteral("stylize"));
    entry(tr("Ripple..."), QStringLiteral("ripple"), QStringLiteral("stylize"));
    entry(tr("Watercolor..."), QStringLiteral("watercolor"), QStringLiteral("stylize"));
    entry(tr("Sunburst..."), QStringLiteral("sunburst"), QStringLiteral("stylize"));
    entry(tr("Recursive Descent..."), QStringLiteral("recursivedescent"),
          QStringLiteral("stylize"));
    entry(tr("Diffuse Glow..."), QStringLiteral("diffuseglow"), QStringLiteral("stylize"));
    entry(tr("Glow (Warped)..."), QStringLiteral("glowwarped"), QStringLiteral("stylize"));
    // Noise
    entry(tr("Add Noise..."), QStringLiteral("addnoise"), QStringLiteral("noise"));
    entry(tr("Clouds..."), QStringLiteral("clouds"), QStringLiteral("noise"));
    entry(tr("Fractal Noise..."), QStringLiteral("fractalnoise"), QStringLiteral("noise"));
    entry(tr("Turbulence..."), QStringLiteral("turbulence"), QStringLiteral("noise"));
    entry(tr("Median..."), QStringLiteral("median"), QStringLiteral("noise"));
    entry(tr("Surface Noise..."), QStringLiteral("surfacenoise"), QStringLiteral("noise"));
    entry(tr("Stretch Dents..."), QStringLiteral("stretchdents"), QStringLiteral("noise"));

    QAction* chosen = menu.exec(QCursor::pos());
    if (!chosen)
        return;
    const QStringList parts = chosen->data().toString().split(QLatin1Char('|'));
    const QString id = parts.value(0);
    const QString category = parts.value(1);

    Layer* layer = m_doc->activeLayer();
    if (!layer)
        return;
    const int layerIdx = m_doc->activeLayerIndex();
    Surface original = layer->surface().copy();
    bool accepted = false;
    const Selection sel = m_doc->selection();

    auto runEffect = [&](Surface& s) {
        const Selection& sl = sel;
        if (id == QLatin1String("gauss"))
            Effects::blurGaussian(s, sl, 3.0, false, false);
        else if (id == QLatin1String("boxblur"))
            Effects::blurBox(s, sl, 5.0, false);
        else if (id == QLatin1String("motion"))
            Effects::blurMotion(s, sl, 45.0, 25.0);
        else if (id == QLatin1String("zoomblur"))
            Effects::blurZoom(s, sl, s.width() / 2, s.height() / 2, 10);
        else if (id == QLatin1String("radialblur"))
            Effects::blurRadial(s, sl, s.width() / 2, s.height() / 2, 10);
        else if (id == QLatin1String("surfaceblur"))
            Effects::blurSurface(s, sl, 50.0, 50.0, 5.0, false, 0);
        else if (id == QLatin1String("sharpen"))
            Effects::sharpen(s, sl, 50.0, 0.0, false);
        else if (id == QLatin1String("unsharp"))
            Effects::unsharpMask(s, sl, 50.0, 3.0, 0);
        else if (id == QLatin1String("glow"))
            Effects::glow(s, sl, 5, 50, 0xFF000000u, false);
        else if (id == QLatin1String("dropshadow"))
            Effects::dropShadow(s, sl, 5, 3, 3, 0xFF000000u, 50);
        else if (id == QLatin1String("innershadow"))
            Effects::innerShadow(s, sl, 5, 3, 3, 0xFF000000u, 50, false);
        else if (id == QLatin1String("emboss"))
            Effects::emboss(s, sl, 50.0, 135.0, 45.0, true);
        else if (id == QLatin1String("invertemboss"))
            Effects::invertEmboss(s, sl, 50.0, 135.0, 45.0);
        else if (id == QLatin1String("edgedetect"))
            Effects::edgeDetect(s, sl, 50.0, 135.0, 45.0);
        else if (id == QLatin1String("pixelate"))
            Effects::pixelate(s, sl, 10, 10, true);
        else if (id == QLatin1String("mosaic"))
            Effects::mosaic(s, sl, 8, 4);
        else if (id == QLatin1String("oilpaint"))
            Effects::oilPaint(s, sl, 4, 8);
        else if (id == QLatin1String("celshading"))
            Effects::celShading(s, sl, 4, 50.0, 50.0);
        else if (id == QLatin1String("vignette"))
            Effects::vignette(s, sl, s.width() / 2, s.height() / 2, 25.0, 100.0, 50.0, false,
                              false, 50.0, 50.0);
        else if (id == QLatin1String("oldphoto"))
            Effects::oldFilm(s, sl, 30, 0, 1, 0, 20.0, 0.0);
        else if (id == QLatin1String("crystalize"))
            Effects::crystalize(s, sl, 12);
        else if (id == QLatin1String("ripple"))
            Effects::ripple(s, sl, 20.0, 12.0, 0.0, 0);
        else if (id == QLatin1String("watercolor"))
            Effects::waterColor(s, sl, 3);
        else if (id == QLatin1String("sunburst"))
            Effects::sunburst(s, sl, s.width() / 2, s.height() / 2, 30, 50.0, false);
        else if (id == QLatin1String("recursivedescent"))
            Effects::recursiveDescent(s, sl, 40.0, true);
        else if (id == QLatin1String("diffuseglow"))
            Effects::diffuseGlow(s, sl, 50.0, 50.0, 1);
        else if (id == QLatin1String("glowwarped"))
            Effects::glowWarped(s, sl, 5, 50, 10, 30, 0xFF000000u, false);
        else if (id == QLatin1String("addnoise"))
            Effects::addNoise(s, sl, 30, true, true, false, 1);
        else if (id == QLatin1String("clouds"))
            Effects::clouds(s, sl, 50.0, 1.0, 0xFF808080u);
        else if (id == QLatin1String("fractalnoise"))
            Effects::fractalNoise(s, sl, 4.0, 0.5, 1, false, 0);
        else if (id == QLatin1String("turbulence"))
            Effects::turbulenceNoise(s, sl, 1);
        else if (id == QLatin1String("median"))
            Effects::medianFilter(s, sl, 4);
        else if (id == QLatin1String("surfacenoise"))
            Effects::surfaceNoise(s, sl, 32, 50.0, false, 1);
        else if (id == QLatin1String("stretchdents"))
            Effects::stretchDents(s, sl, 32, 32, 50.0, QPoint(s.width() / 2, s.height() / 2));
    };

    // Show the parameter dialog for the effect and preview live.
    if (category == QLatin1String("stylize")) {
        StylizeEffectDialog dlg(menu.windowTitle(), id, this);
        connect(&dlg, &StylizeEffectDialog::previewRequested, this, [&, dlgPtr = &dlg] {
            Surface work = original.copy();
            const QString k = dlgPtr->kind();
            const int p0 = dlgPtr->param(0);
            const int p1 = dlgPtr->param(1);
            const int p2 = dlgPtr->param(2);
            if (k == QLatin1String("emboss"))
                Effects::emboss(work, sel, p0, p1, p2, dlgPtr->flag(0));
            else if (k == QLatin1String("invertemboss"))
                Effects::invertEmboss(work, sel, p0, p1, p2);
            else if (k == QLatin1String("edgedetect"))
                Effects::edgeDetect(work, sel, p0, p1, p2);
            else if (k == QLatin1String("pixelate"))
                Effects::pixelate(work, sel, dlgPtr->spin(0), dlgPtr->spin(1), true);
            else if (k == QLatin1String("mosaic"))
                Effects::mosaic(work, sel, dlgPtr->spin(0), dlgPtr->spin(1));
            else if (k == QLatin1String("celshading"))
                Effects::celShading(work, sel, dlgPtr->spin(0), p0, p1);
            else if (k == QLatin1String("oilpaint"))
                Effects::oilPaint(work, sel, dlgPtr->spin(0), dlgPtr->spin(1));
            else if (k == QLatin1String("crystalize"))
                Effects::crystalize(work, sel, dlgPtr->spin(0));
            else if (k == QLatin1String("ripple"))
                Effects::ripple(work, sel, p0, dlgPtr->spin(0), p1, dlgPtr->spin(1));
            else if (k == QLatin1String("watercolor"))
                Effects::waterColor(work, sel, dlgPtr->spin(0));
            else if (k == QLatin1String("sunburst"))
                Effects::sunburst(work, sel, work.width() / 2, work.height() / 2, dlgPtr->spin(0),
                                  p0, dlgPtr->flag(0));
            else if (k == QLatin1String("vignette"))
                Effects::vignette(work, sel, work.width() / 2, work.height() / 2, p0, p1, p2,
                                  dlgPtr->flag(0), dlgPtr->flag(1), 50.0, 50.0);
            else if (k == QLatin1String("oldphoto"))
                Effects::oldFilm(work, sel, p0, dlgPtr->spin(0), dlgPtr->spin(1), dlgPtr->spin(2),
                                 p1, 0.0);
            else if (k == QLatin1String("recursivedescent"))
                Effects::recursiveDescent(work, sel, p0, dlgPtr->flag(0));
            else if (k == QLatin1String("diffuseglow"))
                Effects::diffuseGlow(work, sel, p0, p1, dlgPtr->spin(0));
            else if (k == QLatin1String("glowwarped"))
                Effects::glowWarped(work, sel, dlgPtr->spin(0), dlgPtr->spin(1), dlgPtr->spin(2),
                                    dlgPtr->spin(3), dlgPtr->color(), dlgPtr->flag(0));
            else if (k == QLatin1String("posterizeedges"))
                Effects::posterizeEdges(work, sel, dlgPtr->spin(0), dlgPtr->spin(1), dlgPtr->spin(2),
                                        dlgPtr->spin(3));
            layer->setSurface(work);
            layer->markThumbnailDirty();
            m_doc->notifyLayerPixels(layerIdx, layer->bounds());
        });
        accepted = dlg.exec() == QDialog::Accepted;
    } else if (category == QLatin1String("noise")) {
        NoiseEffectDialog dlg(menu.windowTitle(), id, this);
        connect(&dlg, &NoiseEffectDialog::previewRequested, this, [&, dlgPtr = &dlg] {
            Surface work = original.copy();
            const QString k = dlgPtr->kind();
            if (k == QLatin1String("addnoise"))
                Effects::addNoise(work, sel, dlgPtr->param(0), dlgPtr->flag(0), dlgPtr->flag(1),
                                  dlgPtr->flag(2), dlgPtr->spin(0));
            else if (k == QLatin1String("clouds"))
                Effects::clouds(work, sel, dlgPtr->param(0), dlgPtr->spin(0), dlgPtr->color());
            else if (k == QLatin1String("fractalnoise"))
                Effects::fractalNoise(work, sel, dlgPtr->param(0), dlgPtr->param(1),
                                      dlgPtr->spin(0), dlgPtr->flag(0), 0);
            else if (k == QLatin1String("turbulence"))
                Effects::turbulenceNoise(work, sel, dlgPtr->spin(0));
            else if (k == QLatin1String("median"))
                Effects::medianFilter(work, sel, dlgPtr->spin(0));
            else if (k == QLatin1String("surfacenoise"))
                Effects::surfaceNoise(work, sel, dlgPtr->spin(0), dlgPtr->param(0), dlgPtr->flag(0),
                                      dlgPtr->spin(1));
            else if (k == QLatin1String("stretchdents"))
                Effects::stretchDents(work, sel, dlgPtr->spin(0), dlgPtr->spin(1), dlgPtr->param(0),
                                      QPoint(work.width() / 2, work.height() / 2));
            layer->setSurface(work);
            layer->markThumbnailDirty();
            m_doc->notifyLayerPixels(layerIdx, layer->bounds());
        });
        accepted = dlg.exec() == QDialog::Accepted;
    } else {
        // Blur family: use the dedicated dialogs.
        if (id == QLatin1String("gauss")) {
            GaussianBlurDialog dlg(this);
            connect(&dlg, &GaussianBlurDialog::previewRequested, this, [&] {
                Surface work = original.copy();
                Effects::blurGaussian(work, sel, dlg.radius(), dlg.monochrome(), dlg.deepAnalysis());
                layer->setSurface(work);
                m_doc->notifyLayerPixels(layerIdx, layer->bounds());
            });
            accepted = dlg.exec() == QDialog::Accepted;
        } else if (id == QLatin1String("boxblur")) {
            BoxBlurDialog dlg(this);
            connect(&dlg, &BoxBlurDialog::previewRequested, this, [&] {
                Surface work = original.copy();
                Effects::blurBox(work, sel, dlg.radius(), dlg.monochrome());
                layer->setSurface(work);
                m_doc->notifyLayerPixels(layerIdx, layer->bounds());
            });
            accepted = dlg.exec() == QDialog::Accepted;
        } else if (id == QLatin1String("motion")) {
            MotionBlurDialog dlg(this);
            connect(&dlg, &MotionBlurDialog::previewRequested, this, [&] {
                Surface work = original.copy();
                Effects::blurMotion(work, sel, dlg.angle(), dlg.sampleCount());
                layer->setSurface(work);
                m_doc->notifyLayerPixels(layerIdx, layer->bounds());
            });
            accepted = dlg.exec() == QDialog::Accepted;
        } else if (id == QLatin1String("zoomblur")) {
            RadialBlurDialog dlg(tr("Zoom Blur"), this, true);
            connect(&dlg, &RadialBlurDialog::previewRequested, this, [&] {
                Surface work = original.copy();
                Effects::blurZoom(work, sel, work.width() / 2, work.height() / 2, dlg.amount());
                layer->setSurface(work);
                m_doc->notifyLayerPixels(layerIdx, layer->bounds());
            });
            accepted = dlg.exec() == QDialog::Accepted;
        } else if (id == QLatin1String("radialblur")) {
            RadialBlurDialog dlg(tr("Radial Blur"), this, false);
            connect(&dlg, &RadialBlurDialog::previewRequested, this, [&] {
                Surface work = original.copy();
                Effects::blurRadial(work, sel, work.width() / 2, work.height() / 2, dlg.amount());
                layer->setSurface(work);
                m_doc->notifyLayerPixels(layerIdx, layer->bounds());
            });
            accepted = dlg.exec() == QDialog::Accepted;
        } else if (id == QLatin1String("surfaceblur")) {
            SurfaceBlurDialog dlg(this);
            connect(&dlg, &SurfaceBlurDialog::previewRequested, this, [&] {
                Surface work = original.copy();
                Effects::blurSurface(work, sel, dlg.strength(), dlg.colorStrength(), dlg.size(),
                                     dlg.monochrome(), dlg.seed());
                layer->setSurface(work);
                m_doc->notifyLayerPixels(layerIdx, layer->bounds());
            });
            accepted = dlg.exec() == QDialog::Accepted;
        } else if (id == QLatin1String("sharpen")) {
            SharpenDialog dlg(tr("Sharpen"), this, true);
            connect(&dlg, &SharpenDialog::previewRequested, this, [&] {
                Surface work = original.copy();
                Effects::sharpen(work, sel, dlg.amount(), dlg.radius(), dlg.monochrome());
                layer->setSurface(work);
                m_doc->notifyLayerPixels(layerIdx, layer->bounds());
            });
            accepted = dlg.exec() == QDialog::Accepted;
        } else if (id == QLatin1String("unsharp")) {
            SharpenDialog dlg(tr("Unsharp Mask"), this, false);
            connect(&dlg, &SharpenDialog::previewRequested, this, [&] {
                Surface work = original.copy();
                Effects::unsharpMask(work, sel, dlg.amount(), dlg.radius(), dlg.threshold());
                layer->setSurface(work);
                m_doc->notifyLayerPixels(layerIdx, layer->bounds());
            });
            accepted = dlg.exec() == QDialog::Accepted;
        } else if (id == QLatin1String("glow")) {
            GlowDialog dlg(this);
            connect(&dlg, &GlowDialog::previewRequested, this, [&] {
                Surface work = original.copy();
                Effects::glow(work, sel, dlg.radius(), dlg.intensity(), dlg.glowColor(),
                              dlg.centerAura());
                layer->setSurface(work);
                m_doc->notifyLayerPixels(layerIdx, layer->bounds());
            });
            accepted = dlg.exec() == QDialog::Accepted;
        } else if (id == QLatin1String("dropshadow")) {
            ShadowDialog dlg(tr("Drop Shadow"), this, false);
            connect(&dlg, &ShadowDialog::previewRequested, this, [&] {
                Surface work = original.copy();
                Effects::dropShadow(work, sel, dlg.blurRadius(), dlg.offsetX(), dlg.offsetY(),
                                    dlg.shadowColor(), dlg.opacity());
                layer->setSurface(work);
                m_doc->notifyLayerPixels(layerIdx, layer->bounds());
            });
            accepted = dlg.exec() == QDialog::Accepted;
        } else if (id == QLatin1String("innershadow")) {
            ShadowDialog dlg(tr("Inner Shadow"), this, true);
            connect(&dlg, &ShadowDialog::previewRequested, this, [&] {
                Surface work = original.copy();
                Effects::innerShadow(work, sel, dlg.blurRadius(), dlg.offsetX(), dlg.offsetY(),
                                     dlg.shadowColor(), dlg.opacity(), false);
                layer->setSurface(work);
                m_doc->notifyLayerPixels(layerIdx, layer->bounds());
            });
            accepted = dlg.exec() == QDialog::Accepted;
        }
    }

    if (accepted) {
        const Surface after = layer->surface().copy();
        m_doc->history()->push(new SurfaceAction(chosen->text(), layerIdx, original, after));
        m_doc->notifyLayerPixels(layerIdx, layer->bounds());
        status(tr("%1 applied").arg(chosen->text()));
    } else {
        layer->setSurface(original);
        layer->markThumbnailDirty();
        m_doc->notifyLayerPixels(layerIdx, layer->bounds());
    }
    updateActionStates();
}

// ------------------------------------------------------------------ settings

void MainWindow::openPreferences()
{
    PreferencesDialog dlg(this);
    dlg.setShortcuts(m_toolShortcuts, m_commandShortcuts);
    if (dlg.exec() == QDialog::Accepted) {
        const QMap<QString, QVariant> v = dlg.values();
        dlg.save();
        m_toolShortcuts = dlg.toolShortcuts();
        m_commandShortcuts = dlg.commandShortcuts();
        applyPreferences();
        // Re-apply tool shortcuts.
        for (Tool* t : m_tools->tools()) {
            QAction* a = m_actions[QStringLiteral("tool.") + t->id()];
            if (!a)
                continue;
            a->setShortcut(QKeySequence());
            for (auto it = m_toolShortcuts.constBegin(); it != m_toolShortcuts.constEnd(); ++it) {
                if (it.value().compare(t->name(), Qt::CaseInsensitive) == 0) {
                    a->setShortcut(QKeySequence(it.key()));
                    break;
                }
            }
        }
        status(tr("Preferences saved"));
    }
}

void MainWindow::applyPreferences()
{
    {
        const ThemeMode current = themeMode();
        const QHash<QString, ThemeMode> wanted = {
            { QStringLiteral("view.theme.system"), ThemeMode::System },
            { QStringLiteral("view.theme.light"), ThemeMode::Light },
            { QStringLiteral("view.theme.dark"), ThemeMode::Dark },
        };
        for (auto it = wanted.cbegin(); it != wanted.cend(); ++it)
            if (QAction* a = m_actions.value(it.key()))
                a->setChecked(it.value() == current);
    }
    QSettings s;
    if (!m_doc || !m_canvas || !m_brushes || !m_optionsBar || !m_status)
        return;
    m_doc->setMaxHistoryLength(s.value(QStringLiteral("undo/maxLength"), 20).toInt());
    const bool rulers = s.value(QStringLiteral("ui/rulers"), true).toBool();
    m_canvas->setRulersVisible(rulers);
    if (QAction* a = m_actions[QStringLiteral("view.rulers")])
        a->setChecked(rulers);
    const bool snap = s.value(QStringLiteral("canvas/snap"), true).toBool();
    m_canvas->setSnapToGuides(snap);
    if (QAction* a = m_actions[QStringLiteral("view.snap")])
        a->setChecked(snap);
    m_canvas->setGridSize(s.value(QStringLiteral("canvas/gridSize"), 32).toInt());
    const bool showOptions = s.value(QStringLiteral("ui/alwaysToolOptions"), true).toBool();
    m_optionsBar->setVisible(showOptions);
    m_status->setToolOptionsVisible(showOptions);
    // Brush defaults.
    Brush b = m_brushes->currentBrush();
    b.setSize(s.value(QStringLiteral("brush/size"), 19).toInt());
    b.setHardness(s.value(QStringLiteral("brush/hardness"), 60).toInt());
    b.setSpacing(s.value(QStringLiteral("brush/spacing"), 20).toInt());
    b.setAntiAliasing(s.value(QStringLiteral("brush/aa"), true).toBool());
    b.setShape(static_cast<BrushShape>(s.value(QStringLiteral("brush/shape"), 0).toInt()));
    m_brushes->setBrush(b);
    m_tools->setBrush(b);
}

void MainWindow::showAbout()
{
    AboutDialog dlg(this);
    dlg.exec();
}

void MainWindow::restoreSettings()
{
    QSettings s;
    const QByteArray geom = s.value(QStringLiteral("window/geometry")).toByteArray();
    if (!geom.isEmpty())
        restoreGeometry(geom);
    else
        resize(1280, 820);
    // The palette layout changed from docked panels to floating corner windows.
    // A state blob written by the previous scheme would drag the palettes back
    // into the dock area on every start, so it is dropped once and the new
    // scheme is recorded. After that the user's own arrangement is kept.
    static constexpr int kLayoutVersion = 2;
    const int savedLayout = s.value(QStringLiteral("ui/layoutVersion"), 0).toInt();
    const QByteArray state = s.value(QStringLiteral("window/state")).toByteArray();
    if (savedLayout == kLayoutVersion && !state.isEmpty())
        restoreState(state);
    if (savedLayout != kLayoutVersion)
        s.setValue(QStringLiteral("ui/layoutVersion"), kLayoutVersion);
    applyPreferences();
    const QString tool = s.value(QStringLiteral("window/tool"), QStringLiteral("pencil")).toString();
    setToolById(tool);
    updateRecentFiles();
    const int undoLen = s.value(QStringLiteral("undo/maxLength"), 20).toInt();
    if (m_doc)
        m_doc->setMaxHistoryLength(undoLen);
}

void MainWindow::saveSettings()
{
    QSettings s;
    if (s.value(QStringLiteral("window/savePos"), true).toBool()) {
        s.setValue(QStringLiteral("window/geometry"), saveGeometry());
        s.setValue(QStringLiteral("window/state"), saveState());
    }
    if (s.value(QStringLiteral("window/saveToolState"), true).toBool()) {
        if (Tool* t = m_tools->active())
            s.setValue(QStringLiteral("window/tool"), t->id());
    }
    s.sync();
}

// ------------------------------------------------------------------ events

void MainWindow::showEvent(QShowEvent* e)
{
    QMainWindow::showEvent(e);
    // The stored theme is applied here rather than from the constructor: doing
    // it mid-construction restyled a half-built widget tree and crashed.
    setThemeMode(themeMode());
    onThemeChanged();
    // The canvas has no size until now, so the first "fit" has to wait.
    if (!m_fitted && m_canvas && m_doc) {
        m_fitted = true;
        m_canvas->zoomToFit();
    }
    // During showEvent the window is not mapped yet, so the canvas still has
    // no global position and the palettes would be placed against garbage.
    // Re-anchor once the layout and the window mapping have settled.
    // The canvas keeps being resized as the tool bars and the status bar settle,
    // so retry a few times; the last one runs against the final layout.
    for (int delay : { 0, 60, 200, 500 }) {
        QTimer::singleShot(delay, this, [this] { anchorFloatingPalettes(true); });
    }
}

void MainWindow::resizeEvent(QResizeEvent* e)
{
    QMainWindow::resizeEvent(e);
    // Keep the corner palettes pinned as the window changes size, unless the
    // user has dragged one somewhere else.
    anchorFloatingPalettes();
}

void MainWindow::moveEvent(QMoveEvent* e)
{
    QMainWindow::moveEvent(e);
    // While the window manager is dragging the window, any repaint or geometry
    // change of ours makes it snap the window back to its previous position,
    // which looks like the window flickering between the cursor and the place
    // it started from. Stay completely idle until the move settles.
    if (!m_windowMoving) {
        m_windowMoving = true;
        if (m_canvas)
            m_canvas->setAnimationsEnabled(false);
    }
    if (!m_moveSettle) {
        m_moveSettle = new QTimer(this);
        m_moveSettle->setSingleShot(true);
        m_moveSettle->setInterval(200);
        connect(m_moveSettle, &QTimer::timeout, this, [this] {
            m_windowMoving = false;
            if (m_canvas)
                m_canvas->setAnimationsEnabled(true);
            anchorFloatingPalettes(true);
        });
    }
    m_moveSettle->start();
}

void MainWindow::onCanvasResized()
{
    // Docks and the zoom slider change the canvas rect without resizing the
    // window, so the corner palettes need re-pinning on that too.
    anchorFloatingPalettes();
}

void MainWindow::closeEvent(QCloseEvent* e)
{
    if (m_doc && m_doc->isDirty()) {
        const auto r = QMessageBox::question(
            this, tr("Paint.QT"),
            tr("The current image has unsaved changes.\nDo you want to save them?"),
            QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
        if (r == QMessageBox::Cancel) {
            e->ignore();
            return;
        }
        if (r == QMessageBox::Save && !save()) {
            e->ignore();
            return;
        }
    }
    saveSettings();
    e->accept();
}

void MainWindow::dragEnterEvent(QDragEnterEvent* e)
{
    if (e->mimeData()->hasUrls())
        e->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent* e)
{
    const QList<QUrl> urls = e->mimeData()->urls();
    if (urls.isEmpty())
        return;
    const QString path = urls.first().toLocalFile();
    if (QFileInfo(path).isDir()) {
        // Open all images in the folder.
        const QStringList files = QDir(path).entryList({ QStringLiteral("*.png"), QStringLiteral("*.jpg"),
                                                         QStringLiteral("*.jpeg"), QStringLiteral("*.bmp"),
                                                         QStringLiteral("*.pdq"), QStringLiteral("*.tif") },
                                                      QDir::Files);
        for (const QString& f : files) {
            MainWindow* w = new MainWindow;
            w->show();
            w->openFile(QDir(path).filePath(f));
        }
    } else {
        openFile(path);
    }
    e->acceptProposedAction();
}

void MainWindow::keyPressEvent(QKeyEvent* e)
{
    QMainWindow::keyPressEvent(e);
}

void MainWindow::onDocumentDirtyChanged(bool dirty)
{
    refreshWindowTitle();
    Q_UNUSED(dirty);
}

void MainWindow::onHistoryJump(int index)
{
    if (m_doc)
        m_doc->history()->jumpTo(index);
}

void MainWindow::onCursorMoved(const QPoint& pos)
{
    m_status->setCursorPos(pos);
    m_properties->setCursorPos(pos);
    // Colour under the cursor. It belongs to the status pane, not to
    // QStatusBar::showMessage, whose label would take the left of the bar and
    // push the tool hint out of sight.
    if (m_doc && pos.x() >= 0) {
        const QImage img = m_canvas->compositeImage();
        if (!img.isNull() && pos.x() < img.width() && pos.y() < img.height()) {
            const QRgb c = img.pixel(pos.x(), pos.y());
            m_status->setColorReadout(colorToHex(toPixel(QColor(c))), qAlpha(c));
        } else {
            m_status->setColorReadout(QString(), 0);
        }
    } else {
        m_status->setColorReadout(QString(), 0);
    }
}

void MainWindow::onActiveToolChanged()
{
    refreshToolOptions();
    updateActionStates();
    if (m_canvas->activeTool()) {
        m_status->setToolHint(m_canvas->activeTool()->toolTip());
        m_canvas->setCursor(m_canvas->activeTool()->cursorShape());
    }
    if (m_brushes)
        m_brushes->syncToTool(m_tools->active() ? m_tools->active()->id() : QString());
    if (m_brushes && m_tools->active())
        m_canvas->activeTool()->setBrush(m_brushes->currentBrush());
}

void MainWindow::setToolById(const QString& id)
{
    if (!m_tools->setActiveById(id))
        return;
    QAction* a = m_actions[QStringLiteral("tool.") + id];
    if (a)
        a->setChecked(true);
    Tool* tool = m_tools->active();
    m_canvas->setActiveTool(tool);
    // Finish is only live while the active tool is mid-session, so the new tool
    // has to take over the connection from the previous one.
    if (m_finishConnected) {
        disconnect(m_finishConnected, &Tool::sessionChanged, m_optionsBar,
                   &ToolOptionsBar::setFinishAvailable);
        m_finishConnected = nullptr;
    }
    m_optionsBar->setFinishAvailable(false);
    m_finishConnected = tool;
    connect(m_finishConnected, &Tool::sessionChanged, m_optionsBar,
            &ToolOptionsBar::setFinishAvailable);
}

void MainWindow::onToolOptionsChanged()
{
    refreshToolOptions();
}

void MainWindow::onPaletteVisibilityToggled()
{
    if (m_updatingPalette)
        return;
}

QWidget* MainWindow::createToolOptionsPage()
{
    Tool* tool = m_tools->active();
    if (!tool)
        return nullptr;
    QWidget* page = tool->optionsWidget();
    if (!page) {
        page = tool->createOptionsWidget(m_optionsBar);
        if (page)
            page->setParent(m_optionsBar);
    }
    return page;
}

void MainWindow::refreshToolOptions()
{
    Tool* tool = m_tools->active();
    if (!tool) {
        m_optionsBar->setPage(nullptr, QString());
        return;
    }
    // Rebuild the page so that the controls reflect the current tool state.
    QWidget* page = tool->optionsWidget();
    if (page)
        page->deleteLater();
    page = tool->createOptionsWidget(m_optionsBar);
    m_optionsBar->setPage(page, tool->name());
    m_optionsBar->setFinishAvailable(false);
    resizeOptionsScroll();
}

void MainWindow::resizeOptionsScroll()
{
    if (!m_optionsBar)
        return;
    // Tools with a tall control (text, gradients) need more room than one row.
    const int h = qMax(32, m_optionsBar->sizeHint().height()) + 2;
    if (m_optionsBar->height() != h)
        m_optionsBar->setFixedHeight(h);
}

void MainWindow::updateActionStates()
{
    if (!m_doc)
        return;
    History* h = m_doc->history();
    auto set = [&](const char* id, bool on) {
        if (QAction* a = m_actions.value(QString::fromLatin1(id)))
            a->setEnabled(on);
    };
    set("edit.undo", h->canUndo());
    set("edit.redo", h->canRedo());
    set("edit.paste", !m_clipboard.isNull() || (m_clip && m_clip->hasImage()));
    set("edit.copy", true);
    set("edit.cut", true);
    set("edit.clear", true);
    set("image.croptoselection", m_doc->hasSelection());
    set("file.saveselection", m_doc->hasSelection());
    set("select.grow", m_doc->hasSelection());
    set("select.shrink", m_doc->hasSelection());
    set("select.feather", m_doc->hasSelection());
    set("select.borderselection", m_doc->hasSelection());
    set("select.invert", m_doc->hasSelection());
    set("layers.delete", m_doc->layerCount() > 1);
    set("layers.mergeDown", m_doc->layerCount() > 1);
    set("layers.mergeVisible", m_doc->layerCount() > 1);
    Layer* l = m_doc->activeLayer();
    set("layers.rotate", l != nullptr);
    set("layers.flipH", l != nullptr);
    set("layers.flipV", l != nullptr);
    if (QAction* a = m_actions[QStringLiteral("view.grid")])
        a->setChecked(m_canvas->gridVisible());
    if (QAction* a = m_actions[QStringLiteral("view.snap")])
        a->setChecked(m_canvas->snapToGuides());
    if (QAction* a = m_actions[QStringLiteral("view.rulers")])
        a->setChecked(m_canvas->rulersVisible());
}

void MainWindow::updateWindowIcon()
{
    setWindowIcon(Icons::app());
}

void MainWindow::refreshWindowTitle()
{
    if (!m_doc)
        return;
    const QString name = m_doc->filePath().isEmpty() ? tr("Untitled")
                                                    : QFileInfo(m_doc->filePath()).fileName();
    setWindowTitle(QStringLiteral("%1%2 - Paint.QT")
                       .arg(name, m_doc->isDirty() ? QStringLiteral("*") : QString()));
}

void MainWindow::updateRecentFiles()
{
    if (!m_recentMenu)
        return;
    m_recentMenu->clear();
    for (const QString& path : m_recent->files()) {
        QAction* a = m_recentMenu->addAction(QFileInfo(path).fileName());
        a->setToolTip(path);
        connect(a, &QAction::triggered, this, [this, path] { openFile(path); });
    }
    if (m_recent->files().isEmpty())
        m_recentMenu->addAction(tr("(empty)"))->setEnabled(false);
}

void MainWindow::status(const QString& text, int timeout)
{
    statusBar()->showMessage(text, timeout);
    m_status->setToolHint(text);
}

} // namespace pnq
