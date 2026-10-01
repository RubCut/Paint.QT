#pragma once

#include <QMap>
#include <QString>
#include <QTableWidget>

namespace pnq {

/// A read-only/editable table of tool and action shortcuts, mirroring Paint.NET's
/// "Keyboard Shortcuts" preferences page.
class ShortcutDisplayWidget : public QTableWidget
{
    Q_OBJECT
public:
    enum class Mode { Tools, Commands };

    explicit ShortcutDisplayWidget(QWidget* parent = nullptr, Mode mode = Mode::Tools);

    /// Shortcut -> command name map (both directions are kept in sync).
    QMap<QString, QString> shortcuts() const;
    void setShortcuts(const QMap<QString, QString>& map);
    void resetToDefaults();

    /// Fills the table with the built-in Paint.NET style defaults.
    static QMap<QString, QString> defaultToolShortcuts();
    static QMap<QString, QString> defaultCommandShortcuts();

private:
    void rebuild();
    void applyCell(int row, int column);

    Mode m_mode;
    QMap<QString, QString> m_shortcuts; ///< shortcut -> command
    bool m_updating = false;
};

} // namespace pnq
