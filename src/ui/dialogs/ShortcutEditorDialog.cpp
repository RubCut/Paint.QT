#include "ui/dialogs/ShortcutEditorDialog.h"

#include <QDialogButtonBox>
#include <QLabel>
#include <QTabWidget>
#include <QVBoxLayout>

namespace pnq {

ShortcutEditorDialog::ShortcutEditorDialog(QWidget* parent, bool toolsTab, bool commandsTab)
    : QDialog(parent)
{
    setWindowTitle(tr("Keyboard Shortcuts"));
    setModal(true);
    resize(560, 520);

    QTabWidget* tabs = new QTabWidget(this);
    if (toolsTab) {
        m_tools = new ShortcutDisplayWidget(this, ShortcutDisplayWidget::Mode::Tools);
        tabs->addTab(m_tools, tr("Tools"));
    }
    if (commandsTab) {
        m_commands = new ShortcutDisplayWidget(this, ShortcutDisplayWidget::Mode::Commands);
        tabs->addTab(m_commands, tr("Commands"));
    }
    if (!toolsTab && !commandsTab) {
        m_commands = new ShortcutDisplayWidget(this, ShortcutDisplayWidget::Mode::Commands);
        tabs->addTab(m_commands, tr("Commands"));
    }

    QVBoxLayout* l = new QVBoxLayout(this);
    l->addWidget(new QLabel(
        tr("Click a shortcut cell and press the new key combination. "
           "Press Backspace to clear it."),
        this));
    l->addWidget(tabs);

    QDialogButtonBox* bb =
        new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    l->addWidget(bb);
}

QMap<QString, QString> ShortcutEditorDialog::toolShortcuts() const
{
    return m_tools ? m_tools->shortcuts() : QMap<QString, QString>();
}

QMap<QString, QString> ShortcutEditorDialog::commandShortcuts() const
{
    return m_commands ? m_commands->shortcuts() : QMap<QString, QString>();
}

void ShortcutEditorDialog::resetToDefaults()
{
    if (m_tools)
        m_tools->resetToDefaults();
    if (m_commands)
        m_commands->resetToDefaults();
}

} // namespace pnq
