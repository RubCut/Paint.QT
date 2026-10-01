#include "ui/dialogs/ShortcutDisplayWidget.h"
#include "tools/ToolManager.h"
#include "tools/Tool.h"

#include <QHeaderView>
#include <QKeySequenceEdit>
#include <QSet>

namespace pnq {

QMap<QString, QString> ShortcutDisplayWidget::defaultToolShortcuts()
{
    return {
        { QStringLiteral("B"), QStringLiteral("Pencil") },
        { QStringLiteral("S"), QStringLiteral("Eraser") },
        { QStringLiteral("P"), QStringLiteral("Paint Bucket") },
        { QStringLiteral("F"), QStringLiteral("Gradient") },
        { QStringLiteral("K"), QStringLiteral("Color Picker") },
        { QStringLiteral("T"), QStringLiteral("Text") },
        { QStringLiteral("L"), QStringLiteral("Line/Curve") },
        { QStringLiteral("R"), QStringLiteral("Rectangle") },
        { QStringLiteral("M"), QStringLiteral("Ellipse") },
        { QStringLiteral("A"), QStringLiteral("Freeform Selection") },
        { QStringLiteral("Y"), QStringLiteral("Magic Wand") },
        { QStringLiteral("Ctrl+M"), QStringLiteral("Move Selected") },
        { QStringLiteral("Z"), QStringLiteral("Zoom") },
        { QStringLiteral("H"), QStringLiteral("Pan") },
        { QStringLiteral("X"), QStringLiteral("Swap Colors") },
        { QStringLiteral("Ctrl+B"), QStringLiteral("Paintbrush") },
        { QStringLiteral("Shift+B"), QStringLiteral("Blur Tool") },
        { QStringLiteral("Shift+S"), QStringLiteral("Smudge Tool") },
        { QStringLiteral("Shift+D"), QStringLiteral("Dodge/Burn Tool") },
        { QStringLiteral("Ctrl+R"), QStringLiteral("Rotate") },
        { QStringLiteral("Ctrl+F"), QStringLiteral("Flip") },
        { QStringLiteral("Ctrl+T"), QStringLiteral("Transform") },
    };
}

QMap<QString, QString> ShortcutDisplayWidget::defaultCommandShortcuts()
{
    return {
        { QStringLiteral("Ctrl+N"), QStringLiteral("New") },
        { QStringLiteral("Ctrl+O"), QStringLiteral("Open") },
        { QStringLiteral("Ctrl+S"), QStringLiteral("Save") },
        { QStringLiteral("Ctrl+Shift+S"), QStringLiteral("Save As") },
        { QStringLiteral("Ctrl+Z"), QStringLiteral("Undo") },
        { QStringLiteral("Ctrl+Y"), QStringLiteral("Redo") },
        { QStringLiteral("Ctrl+Shift+Z"), QStringLiteral("Redo") },
        { QStringLiteral("Ctrl+A"), QStringLiteral("Select All") },
        { QStringLiteral("Ctrl+Shift+A"), QStringLiteral("Deselect All") },
        { QStringLiteral("Ctrl+Shift+I"), QStringLiteral("Invert Selection") },
        { QStringLiteral("Ctrl+C"), QStringLiteral("Copy") },
        { QStringLiteral("Ctrl+X"), QStringLiteral("Cut") },
        { QStringLiteral("Ctrl+V"), QStringLiteral("Paste") },
        { QStringLiteral("Delete"), QStringLiteral("Clear") },
        { QStringLiteral("Ctrl+E"), QStringLiteral("Center Layer") },
        { QStringLiteral("Ctrl+Shift+N"), QStringLiteral("New Layer") },
        { QStringLiteral("Ctrl+D"), QStringLiteral("Duplicate Layer") },
        { QStringLiteral("Ctrl+Q"), QStringLiteral("Delete Layer") },
        { QStringLiteral("Ctrl+L"), QStringLiteral("Layer Properties") },
        { QStringLiteral("Ctrl+Shift+E"), QStringLiteral("Merge Layer Down") },
        { QStringLiteral("Ctrl+1"), QStringLiteral("Zoom 100%") },
        { QStringLiteral("Ctrl++"), QStringLiteral("Zoom In") },
        { QStringLiteral("Ctrl+-"), QStringLiteral("Zoom Out") },
        { QStringLiteral("Ctrl+0"), QStringLiteral("Fit On Screen") },
        { QStringLiteral("Ctrl+P"), QStringLiteral("Print") },
    };
}

ShortcutDisplayWidget::ShortcutDisplayWidget(QWidget* parent, Mode mode)
    : QTableWidget(parent)
    , m_mode(mode)
{
    setColumnCount(2);
    QStringList headers;
    headers << tr("Command") << tr("Shortcut");
    setHorizontalHeaderLabels(headers);
    horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    verticalHeader()->setVisible(false);
    setSelectionMode(QAbstractItemView::NoSelection);
    setEditTriggers(QAbstractItemView::NoEditTriggers);
    setAlternatingRowColors(true);

    m_shortcuts = (mode == Mode::Tools) ? defaultToolShortcuts() : defaultCommandShortcuts();
    rebuild();
}

void ShortcutDisplayWidget::rebuild()
{
    m_updating = true;
    setRowCount(m_shortcuts.size());
    int row = 0;
    for (auto it = m_shortcuts.constBegin(); it != m_shortcuts.constEnd(); ++it, ++row) {
        auto* nameItem = new QTableWidgetItem(it.value());
        nameItem->setFlags(Qt::ItemIsEnabled);
        setItem(row, 0, nameItem);

        auto* edit = new QKeySequenceEdit(QKeySequence(it.key()), this);
        setCellWidget(row, 1, edit);
        connect(edit, &QKeySequenceEdit::keySequenceChanged, this, [this, row, edit] {
            if (m_updating)
                return;
            applyCell(row, 2);
            Q_UNUSED(edit);
        });
    }
    m_updating = false;
}

void ShortcutDisplayWidget::applyCell(int row, int)
{
    auto* edit = qobject_cast<QKeySequenceEdit*>(cellWidget(row, 1));
    if (!edit || m_updating)
        return;
    const QString name = item(row, 0)->text();
    // Remove any previous binding for this command, then rebind.
    for (auto it = m_shortcuts.begin(); it != m_shortcuts.end();) {
        if (it.value() == name)
            it = m_shortcuts.erase(it);
        else
            ++it;
    }
    const QString seq = edit->keySequence().toString(QKeySequence::PortableText);
    if (!seq.isEmpty())
        m_shortcuts.insert(seq, name);
}

QMap<QString, QString> ShortcutDisplayWidget::shortcuts() const
{
    return m_shortcuts;
}

void ShortcutDisplayWidget::setShortcuts(const QMap<QString, QString>& map)
{
    m_shortcuts = map;
    rebuild();
}

void ShortcutDisplayWidget::resetToDefaults()
{
    m_shortcuts = (m_mode == Mode::Tools) ? defaultToolShortcuts() : defaultCommandShortcuts();
    rebuild();
}

} // namespace pnq
