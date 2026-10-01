# Paint.QT

Paint.NET recreated on Qt 6.

A raster image editor written in C++20 on Qt 6. It follows [Paint.NET][pdn]: the same
tools, the same palette layout, the same `.pdn` file format, and the same history where
one stroke is one undo step.

![Paint.QT](packaging/icons/paint-qt-128.png)

The window draws its own light chrome, so it looks the same on every desktop instead of
inheriting whatever a platform's widget style happens to be.

## What is in it

**Tools (28)**

| Group | Tools |
| --- | --- |
| Drawing | Pencil, paintbrush, eraser, colour picker, gradient |
| Retouching | Blur, smudge, dodge and burn, recolour |
| Shapes | Line, rectangle, ellipse, polygon, freeform, curve |
| Text | Text with a live editing box |
| Selection | Marquee, ellipse, lasso, select opaque, move |
| Transform | Rotate, flip, scale |

**Layers** with opacity, locking, visibility, merging and flattening.

**History** where every stroke is its own step. Two strokes never collapse into one
Ctrl+Z.

**The clipboard is the system clipboard.** Copy an image in a browser, paste it here; copy
a selection here, paste it into anything that takes an image.

**`.pdn` files open and save losslessly**, so a document can move between Paint.NET and
Paint.QT in either direction.

**Freehand strokes are smoothed** along a Catmull-Rom spline through the mouse samples, so
a fast drag comes out smooth instead of as visible chords.

## Building

Needs CMake 3.21 or newer, a C++20 compiler, and Qt 6.2 or newer (`Core`, `Gui`, `Widgets`,
`Concurrent`; `Svg` is optional but recommended).

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
./build/bin/paint-qt
```

### Dependencies

| Platform | Packages |
| --- | --- |
| Debian, Ubuntu | `build-essential cmake ninja-build qt6-base-dev libqt6svg6-dev` |
| Fedora | `gcc-c++ cmake ninja-build qt6-qtbase-devel qt6-qtsvg-devel` |
| Arch | `base-devel cmake ninja qt6-base qt6-svg` |
| macOS (Homebrew) | `cmake ninja qt` |

## Running the tests

```sh
cd build && ctest --output-on-failure
```

Six suites:

| Suite | What it checks |
| --- | --- |
| `core_tests` | Document, history, selections, image maths |
| `gui_smoke` | The real window: layout, palettes, clipboard, zoom, undo |
| `drag_paint` | A stroke through real Qt mouse events, then undo, redo and a `.pdn` round trip |
| `tool_audit` | All 28 tools driven with real mouse events; each one has to change pixels |
| `icons` | Every tool and command has its own artwork, and the mark exists at every size |
| `packages` | Every file the packaging reads, checked before a release nobody looked at |

The `icons` suite writes `/tmp/pnq_icons.png`, a contact sheet of all the artwork.

## Installing

```sh
cmake --install build --prefix /usr/local
```

That puts the binary in `bin`, the icon into `hicolor`, and a `.desktop` file plus AppStream
metadata into `share/`.

## Packages

All packaging lives in `packaging/` and is driven by the same CMake variables, so a version
bump updates every format at once.

### Linux (Debian, RPM, tarball)

Paint.QT targets Linux and the BSDs.

```sh
cd build
cpack                      # TGZ, DEB and RPM
cpack --config CPackSourceConfig.cmake   # the source tarball
```

The `.deb` and the `.rpm` carry the icon, the `.desktop` file and the AppStream metadata,
so the application shows up in a menu, in a file chooser and in the software centre with
its own artwork.

The icon goes into `share/icons/hicolor` at every size, so a panel asks for the 32 px file
and a file manager's large view asks for the 256 px one, neither scaled from the other.

### macOS

The same CMake produces a working bundle, with the `.icns` as its icon:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build && cd build && cpack -G DragNDrop
```

### Flatpak

```sh
packaging/flatpak/build-flatpak.sh --verify
```

The manifest uses `org.kde.Platform//6.7` because the program is a KDE-flavoured Qt
application. It requests no privileges beyond the defaults plus the picture, document and
download directories, so the file dialog opens through the portal.

### Arch (AUR)

`packaging/aur/PKGBUILD` and its generated `packaging/aur/.SRCINFO` are ready to push:

```sh
git clone <your-aur-pkgbuild-repo>
cp packaging/aur/PKGBUILD packaging/aur/.SRCINFO .
makepkg -si
```

The PKGBUILD builds from the release tarball. To package an unreleased commit, use the
`git+https` form noted in its header.

## The logo

The mark is the Paint.NET logo's composition: a frame with a brush laid across it on the
diagonal. What is inside the frame is not a photograph but a window this program would
really show, in the KDE Breeze idiom, with a swash of paint on its canvas.

It exists twice, deliberately:

- `packaging/icons/paint-qt.svg` ships to the icon theme and is rasterised into every PNG,
  the `.ico` and the `.icns` at build time by `tools/make_icons.cpp`.
- `src/resources/Icons.cpp` draws it procedurally, so the running application needs no
  files at all.

The `icons` test renders both and compares them, so editing one without the other fails the
build rather than quietly shipping two different logos.

## Project layout

```
src/core/        document, layers, history, selections, image maths, effects
src/tools/       the 28 tools and their option panels
src/ui/          main window, canvas view, palettes, theme, dialogs
src/resources/   procedural icons and the colour palettes
tests/           six test suites
tools/           build time helpers
packaging/       everything that produces an installable package
```

## Licence

GPL-3.0-or-later. See [LICENSE](LICENSE).

Paint.NET is a registered trademark of its owner. This is an independent reimplementation and
no affiliation or endorsement is implied.

[pdn]: https://www.getpaint.net/