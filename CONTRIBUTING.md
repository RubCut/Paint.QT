# Contributing

## Building and testing

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

The suites run headless: they use Qt's `offscreen` platform, so no display is
needed and nothing has to be windowed by hand.

Two of them write images you can look at, which is usually faster than reading a
failure message:

| Command | Writes |
| --- | --- |
| `./build/bin/pnq_icons` | `/tmp/pnq_icons.png`, a sheet of every icon |
| `./build/bin/pnq_draw_shot` | a screenshot of the window after drawing |

## The tests are the specification

There are six suites and each one exists because something was wrong once:

| Suite | Why it exists |
| --- | --- |
| `core_tests` | Document, history, selections, image maths |
| `gui_smoke` | The real window: layout, palettes, clipboard, zoom, undo |
| `drag_paint` | A drag painting nothing cannot hide behind a unit test |
| `tool_audit` | A tool that never touches a pixel must fail, not pass quietly |
| `icons` | Two ids falling through to one placeholder look identical, so pixels are compared |
| `packages` | Every file the packaging reads, checked before a release nobody looked at |

If you change behaviour, add the check that would have caught the bug before it
happened again. A test that asserts a property rather than a value survives
refactoring; a test that asserts one exact pixel rarely does.

## Style

The existing code sets the conventions: 4 spaces, 110 column lines, `m_` for members,
`PascalCase` for methods, trailing `_` for Qt overrides.

Two things matter more than formatting:

**Comments explain why, not what.** The code already says what it does. A comment earns
its place by explaining a decision that would otherwise look arbitrary:

```cpp
// The drag offset is measured in the panel's own coordinates, so feeding it to
// the parent threw it at the origin on the very first move.
```

**Measure before believing.** If something looks wrong, print the actual state
(`geometry()`, `pos()`, the pixels) rather than reasoning about what the code
probably does. Several bugs in this project's history were misdiagnosed twice
because a plausible explanation was acted on without a measurement.

## Icons

Two files draw the application mark and both must agree:

- `packaging/icons/paint-qt.svg` ships to the icon theme and is rasterised at build time.
- `src/resources/Icons.cpp` draws it procedurally so the running app needs no files.

The `icons` suite renders both and compares them, so changing one without the other fails
the build. Do not edit the generated PNGs, `.ico` or `.icns` by hand; run
`pnq_make_icons` or just build.

## Packaging

Everything lives in `packaging/` and is driven by the variables at the top of
`CMakeLists.txt`, so a version bump updates every format at once.

When you add a tool or a command, give it artwork. The `icons` suite compares rendered
pixels, so two ids with no drawing of their own fail it. `tools/make_icons.cpp` is the icon
renderer; `packaging/aur/PKGBUILD` and the Flatpak manifest are the per-distribution
recipes.

Never bump the version in an `Info.plist` or a PKGBUILD by hand: they read it from CMake, and
`packages` checks that they do.

## Pull requests

Say what changed and what you measured. A short list of what the tests now catch that they
did not before is more useful than a description of the diff.

Keep unrelated changes in separate commits. A behavioural fix and a formatting pass in one
commit cannot be reviewed or reverted independently.