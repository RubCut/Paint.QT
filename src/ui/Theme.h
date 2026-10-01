#pragma once

#include <QString>

namespace pnq {

/// Which appearance Paint.QT uses.
enum class ThemeMode {
    System, ///< follow the desktop light/dark setting and react to changes
    Light,
    Dark,
};

/// The application look. Paint.NET draws a light chrome of its own; this
/// restyles the whole widget set to match, in both a light and a dark variant
/// so the app can follow the desktop setting.
QString styleSheetFor(bool dark);

/// Applies the look to the whole application. Safe to call more than once: the
/// work happens on the first call so every entry point ends up with the look.
void applyTheme();

/// Switches mode, persisting it, and re-applies the look immediately.
void setThemeMode(ThemeMode mode);
ThemeMode themeMode();

/// Re-reads the desktop setting and swaps variants if it changed. Called
/// automatically when the platform reports a colour scheme change.
void syncThemeWithSystem();

/// The mode actually in effect, after resolving System.
bool isDarkThemeInEffect();

} // namespace pnq
