#pragma once

#include <QColor>
#include <QIcon>
#include <QPixmap>

namespace pnq {

namespace Icons {

/// Icon for a tool id ("pencil", "eraser", ...). Drawn procedurally so the
/// application ships without binary artwork.
QIcon tool(const QString& id);

/// Colour used for the line art of the tool and command icons. The application
/// sets it from the active theme so the icons stay readable on a dark chrome.
void setInk(const QColor& colour);
/// Drops the cached pixmaps so they are redrawn with the new ink.
void clearCache();
QPixmap toolPixmap(const QString& id, int size);
QIcon app();
/// Icon for a command-bar action ("new", "open", "save", ...).
QIcon command(const QString& id);
QPixmap commandPixmap(const QString& id, int size);

/// Kept for callers that pass either kind of id.
QIcon action(const QString& id);
QPixmap pixmap(const QString& id, int size);

} // namespace Icons
} // namespace pnq
