# Changelog

Every release is a GitHub release at
[RubCut/Paint.QT/releases](https://github.com/RubCut/Paint.QT/releases). This file is
the same list in one place.

## 1.0.1 — 2026-10-02

The packaging metadata is corrected. In 1.0.0 some of it pointed at things that do
not exist, and one omission stopped the Flatpak build outright.

- The AppStream component names an icon. Without one `appstreamcli` treats the
  component as invisible and refuses to compose it, which is what kept the Flatpak
  from finishing.
- The application id matches the repository it lives in, `io.github.RubCut.Paint.QT`.
  It was `io.github.paintqt.Paint.QT`, naming a GitHub account that does not exist.
- The packages carry the real homepage and a real contact address.
- The licence is installed where each distribution looks for it:
  `licenses/<package>/LICENSE` for Arch and RPM, `doc/<package>/copyright` for
  Debian.
- The desktop entry and every icon are named after the application id, which Flatpak
  and Flathub both require. Flatpak had been dropping all of them, leaving the
  package with no icon.
- The PKGBUILD builds from the actual archive for its tag and verifies its checksum.
- The `.flatpakref` is cut to the shape GNOME Software accepts, and carries the
  public key inline, so installing it needs no separate key import.

## 1.0.0 — 2026-10-01

First release.

- 28 tools: drawing, retouching, shapes, text, selection, transform.
- Layers with opacity, locking, visibility, merging and flattening.
- History where every stroke is its own undo step.
- The system clipboard, both directions.
- `.pdn` files read and written losslessly.
- Freehand strokes smoothed along a Catmull-Rom spline.
- Light chrome of the program's own, so the window looks the same on every desktop.
- Debian, RPM, tarball, macOS bundle, Flatpak, AppImage and an Arch recipe.