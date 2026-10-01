// The Text tool.
//
// Clicking on the canvas opens a small inline editor (a QTextEdit parented to
// the CanvasView) that floats over the image. Enter or losing the focus
// renders the text into the active layer as a single undoable step.

#include "tools/Tool.h"

#include "core/Document.h"
#include "core/History.h"
#include "core/Selection.h"
#include "core/Surface.h"
#include "tools/ToolManager.h"
#include "tools/ToolSupport.h"
#include "ui/CanvasView.h"
#include "ui/ToolOptions.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QEvent>
#include <QFontDatabase>
#include <QFontMetrics>
#include <QFrame>
#include <QKeyEvent>
#include <QLabel>
#include <QPainter>
#include <QPen>
#include <QPointer>
#include <QSpinBox>
#include <QTextEdit>
#include <QBoxLayout>

namespace pnq {
namespace {

// ------------------------------------------------------------------ helpers

/// Source-over copy of `src` (anchored at `dstPos`) into a surface.
void blitRegion(Surface& dst, const QPoint& dstPos, const QImage& src, const QImage* mask)
{
    if (src.isNull())
        return;
    const QRect clip = QRect(dstPos, src.size()).intersected(dst.bounds());
    for (int y = clip.top(); y <= clip.bottom(); ++y) {
        const int sy = y - dstPos.y();
        pixel_t* d = dst.scanLine(y);
        const pixel_t* s = reinterpret_cast<const pixel_t*>(src.constScanLine(sy));
        const uchar* m = mask ? mask->constScanLine(sy) : nullptr;
        for (int x = clip.left(); x <= clip.right(); ++x) {
            const int sx = x - dstPos.x();
            pixel_t sp = s[sx];
            if (sp == 0)
                continue;
            if (m) {
                const int v = m[sx];
                if (v == 0)
                    continue;
                if (v != 255)
                    sp = qPremult(quint8(getA(sp) * v / 255), quint8(getR(sp) * v / 255),
                                  quint8(getG(sp) * v / 255), quint8(getB(sp) * v / 255));
            }
            d[x] = composePixel(d[x], sp, BlendMode::Normal);
        }
    }
}

void drawDashedRect(QPainter& p, const QRectF& r, const QColor& color)
{
    QPen pen(color);
    pen.setCosmetic(true);
    pen.setStyle(Qt::DashLine);
    pen.setDashPattern({ 4.0, 3.0 });
    p.save();
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    p.drawRect(r);
    p.restore();
}

/// Commits a pixel change as exactly one, non mergeable history step
/// (PixelDeltaAction::isMergeable() is true, so consecutive text boxes would
/// otherwise be merged into a single undo step).
void pushShapeAction(Tool* tool, const QString& name, int layerIndex, const QRect& rect,
                     const Surface& before, const Surface& after)
{
    if (!tool || !tool->document() || rect.isEmpty())
        return;
    Document* doc = tool->document();
    if (!doc->history())
        return;
    MacroAction* macro = new MacroAction(name);
    macro->add(new PixelDeltaAction(name, layerIndex, rect, before, after));
    doc->history()->push(macro);
}

// ---------------------------------------------------------------- the tool

class TextTool : public Tool
{
public:
    explicit TextTool(QObject* parent = nullptr) : Tool(parent)
    {
        m_font.setPointSize(m_size);
        // Losing the focus (i.e. clicking the canvas) commits the text.
        if (QApplication* app = qobject_cast<QApplication*>(QCoreApplication::instance())) {
            connect(app, &QApplication::focusChanged, this, [this](QWidget* old, QWidget* now) {
                if (!m_editor.isNull() && old == m_editor.data() && now != m_editor.data())
                    commitEditor();
            });
        }
    }

    ~TextTool() override { destroyEditor(); }

    // ------------------------------------------------------------- identity
    QString id() const override { return QStringLiteral("text"); }
    QString name() const override { return tr("Text"); }
    QString toolTip() const override { return tr("Click and drag to add a text box, then type"); }
    QString shortcutString() const override { return QStringLiteral("T"); }
    int sortOrder() const override { return 400; }
    Qt::CursorShape cursorShape() const override { return Qt::IBeamCursor; }

    QString statusText() const override
    {
        if (!m_editor.isNull())
            return tr("Type your text, press Enter to apply, Escape to cancel");
        if (m_dragging)
            return tr("%1 x %2").arg(m_dragRect.width()).arg(m_dragRect.height());
        return tr("Click the image to add text");
    }

    // -------------------------------------------------------------- options
    QWidget* createOptionsWidget(QWidget* parent) override
    {
        QBoxLayout* layout = nullptr;
        m_optionsWidget = ToolOptions::createRoot(parent, &layout);

        m_familyBox = qobject_cast<QComboBox*>(ToolOptions::addCombo(
            layout, tr("Font"), QFontDatabase::families(),
            qMax(0, QFontDatabase::families().indexOf(m_font.family()))));
        connect(m_familyBox, &QComboBox::currentTextChanged, this, [this](const QString& f) {
            m_font.setFamily(f);
            applyFontToEditor();
        });

        m_sizeBox =
            qobject_cast<QSpinBox*>(ToolOptions::addSpin(layout, tr("Size"), 4, 400, m_size, tr(" pt")));
        connect(m_sizeBox, &QSpinBox::valueChanged, this, [this](int v) {
            m_size = v;
            m_font.setPointSize(v);
            applyFontToEditor();
        });

        m_boldBox = qobject_cast<QCheckBox*>(ToolOptions::addCheck(layout, tr("Bold"), m_bold));
        connect(m_boldBox, &QCheckBox::toggled, this, [this](bool v) {
            m_bold = v;
            applyFontToEditor();
        });
        m_italicBox = qobject_cast<QCheckBox*>(ToolOptions::addCheck(layout, tr("Italic"), m_italic));
        connect(m_italicBox, &QCheckBox::toggled, this, [this](bool v) {
            m_italic = v;
            applyFontToEditor();
        });
        m_underlineBox =
            qobject_cast<QCheckBox*>(ToolOptions::addCheck(layout, tr("Underline"), m_underline));
        connect(m_underlineBox, &QCheckBox::toggled, this, [this](bool v) {
            m_underline = v;
            applyFontToEditor();
        });

        m_modeBox = qobject_cast<QComboBox*>(
            ToolOptions::addCombo(layout, tr("Mode"), { tr("Fill"), tr("Outline") }, 0));
        connect(m_modeBox, &QComboBox::currentIndexChanged, this,
                [this](int i) { m_mode = i; });
        m_outlineBox = qobject_cast<QSpinBox*>(
            ToolOptions::addSpin(layout, tr("Outline width"), 1, 64, m_outlineWidth, tr(" px")));
        connect(m_outlineBox, &QSpinBox::valueChanged, this,
                [this](int v) { m_outlineWidth = v; });

        ToolOptions::addSeparator(layout);
        m_aaBox = qobject_cast<QCheckBox*>(ToolOptions::addCheck(layout, tr("Anti-aliasing"), true));
        connect(m_aaBox, &QCheckBox::toggled, this, [this](bool v) { m_antialias = v; });
        m_bgBox =
            qobject_cast<QCheckBox*>(ToolOptions::addCheck(layout, tr("Auto fill background"), false));
        connect(m_bgBox, &QCheckBox::toggled, this, [this](bool v) { m_autoBackground = v; });

        return m_optionsWidget;
    }

    // ------------------------------------------------------------ lifecycle
    void activate() override
    {
        m_dragging = false;
        m_dragRect = QRect();
        Tool::activate();
    }

    void deactivate() override
    {
        commitEditor();
        m_dragging = false;
        m_dragRect = QRect();
        requestOverlayUpdate();
        Tool::deactivate();
    }

    // ---------------------------------------------------------------- input
    void mouseDown(const QPoint& docPos, Qt::MouseButton button, Qt::KeyboardModifiers) override
    {
        if (button == Qt::RightButton) {
            cancelEditor();
            return;
        }
        // Only one text box at a time.
        commitEditor();
        if (!document())
            return;
        m_press = docPos;
        m_cur = docPos;
        m_dragging = true;
        m_dragRect = QRect(docPos, QSize(1, 1));
        requestOverlayUpdate();
    }

    void mouseMove(const QPoint& docPos, Qt::MouseButtons, Qt::KeyboardModifiers) override
    {
        if (!m_dragging)
            return;
        m_cur = docPos;
        m_dragRect = QRect(m_press, m_cur).normalized().adjusted(0, 0, 1, 1);
        requestOverlayUpdate();
        refreshStatus();
    }

    void mouseUp(const QPoint& docPos, Qt::MouseButton, Qt::KeyboardModifiers) override
    {
        if (!m_dragging)
            return;
        m_cur = docPos;
        m_dragging = false;
        QRect box = QRect(m_press, m_cur).normalized();
        if (box.width() < 8 || box.height() < QFontMetrics(m_font).height())
            box = QRect(m_press, QSize(200, QFontMetrics(m_font).height() * 3));
        else
            box.setSize(box.size() + QSize(1, 1));
        m_dragRect = QRect();
        openEditor(box);
        requestOverlayUpdate();
    }

    void keyPressEvent(QKeyEvent* e) override
    {
        if (e->key() == Qt::Key_Escape && !m_editor.isNull()) {
            cancelEditor();
            e->accept();
            return;
        }
        Tool::keyPressEvent(e);
    }

    bool eventFilter(QObject* obj, QEvent* e) override
    {
        if (!m_editor.isNull() && obj == m_editor.data() && e->type() == QEvent::KeyPress) {
            QKeyEvent* ke = static_cast<QKeyEvent*>(e);
            if (ke->key() == Qt::Key_Escape) {
                cancelEditor();
                return true;
            }
            if ((ke->key() == Qt::Key_Return || ke->key() == Qt::Key_Enter)
                && !(ke->modifiers() & Qt::ShiftModifier)) {
                commitEditor();
                return true;
            }
        }
        return Tool::eventFilter(obj, e);
    }

    // ------------------------------------------------------------- painting
    void drawOverlay(QPainter& p) override
    {
        if (m_dragging && !m_dragRect.isEmpty())
            drawDashedRect(p, QRectF(m_dragRect), toQColor(primaryColor()));
    }

private:
    // ---------------------------------------------------------- the editor
    void openEditor(const QRect& box)
    {
        if (!view() || !document())
            return;
        m_box = box;
        m_editor = new QTextEdit(view());
        m_editor->setFrameShape(QFrame::NoFrame);
        m_editor->setAttribute(Qt::WA_TranslucentBackground);
        m_editor->setAcceptRichText(false);
        m_editor->setFont(m_font);
        m_editor->setTextColor(toQColor(primaryColor()));
        m_editor->setStyleSheet(QStringLiteral("QTextEdit{ background: transparent; }"));
        m_editor->setGeometry(view()->imageRectToWidget(m_box));
        m_editor->installEventFilter(this);
        m_editor->show();
        m_editor->setFocus();
        connect(m_editor.data(), &QTextEdit::textChanged, this, [this]() { growBoxToFitText(); });
        requestOverlayUpdate();
    }

    void applyFontToEditor()
    {
        if (m_editor.isNull())
            return;
        m_editor->setFont(m_font);
        m_editor->setTextColor(toQColor(primaryColor()));
    }

    /// Keeps the frame around the text while typing.
    void growBoxToFitText()
    {
        if (m_editor.isNull() || !view())
            return;
        const QSizeF sz = m_editor->document()->size();
        QRect r = m_box;
        r.setWidth(qMax(r.width(), qCeil(sz.width())));
        r.setHeight(qMax(r.height(), qCeil(sz.height())));
        if (r == m_box)
            return;
        m_box = r;
        m_editor->setGeometry(view()->imageRectToWidget(m_box));
    }

    void commitEditor()
    {
        if (m_editor.isNull())
            return;
        const QString text = m_editor->toPlainText();
        const QRect box = m_box;
        destroyEditor();
        if (text.trimmed().isEmpty())
            return;
        renderText(text, box);
    }

    void cancelEditor()
    {
        if (m_editor.isNull())
            return;
        destroyEditor();
    }

    void destroyEditor()
    {
        if (m_editor.isNull())
            return;
        QTextEdit* ed = m_editor.data();
        m_editor = nullptr;
        ed->removeEventFilter(this);
        ed->hide();
        ed->deleteLater();
        requestOverlayUpdate();
    }

    // ------------------------------------------------------------ rendering
    void renderText(const QString& text, const QRect& box)
    {
        Document* doc = document();
        if (!doc || !canPaint() || box.isEmpty())
            return;
        Layer* l = activeLayer();
        if (!l)
            return;
        const int layerIndex = doc->activeLayerIndex();
        QImage img(box.width(), box.height(), QImage::Format_ARGB32_Premultiplied);
        img.fill(Qt::transparent);
        {
            QPainter p(&img);
            p.setRenderHint(QPainter::TextAntialiasing, m_antialias);
            p.setFont(m_font);
            if (m_autoBackground)
                p.fillRect(img.rect(), toQColor(secondaryColor()));
            if (m_mode == 1) {
                QPen pen(toQColor(primaryColor()));
                pen.setWidthF(m_outlineWidth);
                pen.setJoinStyle(Qt::RoundJoin);
                p.setPen(pen);
                p.setBrush(Qt::NoBrush);
            } else {
                p.setPen(Qt::NoPen);
                p.setBrush(toQColor(primaryColor()));
            }
            p.drawText(QRectF(img.rect()), Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap, text);
        }
        const int pad = m_outlineWidth + 2;
        const QRect area = box.adjusted(-pad, -pad, pad, pad).intersected(l->bounds());
        if (area.isEmpty())
            return;

        // Restrict the text to the selection when there is one.
        QImage mask;
        if (doc->hasSelection()) {
            Selection local(box.width(), box.height());
            local.setMask(doc->selection().mask().copy(box));
            mask = local.mask();
        }

        StrokeBuffer buf;
        buf.begin(&l->surface());
        buf.touch(area);
        blitRegion(l->surface(), box.topLeft(), img, mask.isNull() ? nullptr : &mask);
        const QRect dirty = buf.end();
        if (dirty.isEmpty())
            return;
        const Surface before = buf.before();
        const Surface after = buf.after();
        if (before == after)
            return;
        l->markThumbnailDirty();
        doc->notifyLayerPixels(layerIndex, dirty);
        pushShapeAction(this, tr("Text"), layerIndex, dirty, before, after);
        requestUpdate(dirty);
    }

    // --------------------------------------------------------------- state
    QFont m_font;
    int m_size = 12;
    bool m_bold = false;
    bool m_italic = false;
    bool m_underline = false;
    bool m_antialias = true;
    bool m_autoBackground = false;
    int m_mode = 0;
    int m_outlineWidth = 2;

    QComboBox* m_familyBox = nullptr;
    QSpinBox* m_sizeBox = nullptr;
    QCheckBox* m_boldBox = nullptr;
    QCheckBox* m_italicBox = nullptr;
    QCheckBox* m_underlineBox = nullptr;
    QComboBox* m_modeBox = nullptr;
    QSpinBox* m_outlineBox = nullptr;
    QCheckBox* m_aaBox = nullptr;
    QCheckBox* m_bgBox = nullptr;

    QPointer<QTextEdit> m_editor;
    QRect m_box;
    QPoint m_press;
    QPoint m_cur;
    QRect m_dragRect;
    bool m_dragging = false;
};

} // namespace

// ------------------------------------------------------------------ factory

QList<Tool*> createTextTools()
{
    return { new TextTool(nullptr) };
}

} // namespace pnq
