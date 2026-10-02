<!-- What a pull request needs to be checkable by someone who was not in the room. -->

## What this changes

<!-- One or two sentences. If the behaviour differs from Paint.NET, say where. -->

## How it was checked

<!--
Which of these you ran, and what you saw:

  cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
  cmake --build build -j"$(nproc)"
  ctest --test-dir build --output-on-failure

The six suites cover the document, the window, a real drag, all 28 tools, the
artwork, and the packaging. A change to an icon needs the artwork suite: it renders
the procedural mark and the SVG and compares them, so editing one without the other
fails.
-->

- [ ] Built and the suites pass
- [ ] Tried it in the window, not only in the tests

## Before it is finished

<!--
Checklist for the kind of change this is. Delete the rest.

  Behaviour:      a note in CHANGELOG.md, and a release note if it is user visible
  Packaging:      a version in CMakeLists.txt, and the PKGBUILD sha256 for the tag
  Icons:          regenerate rather than hand draw, and let the icons suite compare
  Metadata:       appstreamcli validate --no-net packaging/linux/io.github.RubCut.Paint.QT.metainfo.xml
  Artwork:        tools/make_icons.cpp reads the SVG; it is the source for all of it
-->

- [ ] CHANGELOG.md has an entry, if this is user visible
- [ ] Version bumped, if this is a release
- [ ] AppStream metadata revalidated, if the metainfo or the desktop entry changed