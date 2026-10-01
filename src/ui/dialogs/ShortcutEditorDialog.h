#pragma once

#include "ui/dialogs/ShortcutDisplayWidget.h"

#include <QDialog>
#include <QMap>
#include <QString>

namespace pnq {

/// Standalone keyboard shortcuts editor, matching Paint.NET's own dialog.
class ShortcutEditorDialog : public QDialog
{
    Q_OBJECT
public:
    explicit ShortcutEditorDialog(QWidget* parent = nullptr, bool toolsTab = true,
                                  bool commandsTab = true);

    QMap<QString, QString> toolShortcuts() const;
    QMap<QString, QString> commandShortcuts() const;
    void resetToDefaults();

private:
    ShortcutDisplayWidget* m_tools = nullptr;
    ShortcutDisplayWidget* m_commands = nullptr;
};

} // namespace pnq
