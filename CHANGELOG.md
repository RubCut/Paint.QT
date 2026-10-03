# Changelog

Every release is a GitHub release at
[RubCut/Paint.QT/releases](https://github.com/RubCut/Paint.QT/releases). This file is
the same list in one place.

## 1.0.3 — 2026-10-03

Paint.NET files are read, and pressing Open no longer closes the program.

**Paint.NET's `.pdn` opens.** Every layer, with its name, visibility, background flag,
opacity, blend mode and pixels. Checked layer by layer against the reference reader on
ten files covering one, two, three and fourteen layers, all fourteen blend modes, and
Paint.NET 3.510 as well as 4.21:

```
layers compared: 28   files with problems: 0   layers with pixel differences: 0
worst single-channel pixel difference anywhere: 0
```

Paint.NET's format is a binary container beginning with `PDN3`, followed by a .NET object
graph and gzipped pixel blocks that are split into numbered chunks arriving in no
particular order. Three things about it are not guessable and each was found the hard
way:

- A member declared as a primitive is read from its type code, but a member declared as
  a string or as a primitive array arrives as a record like anything else. Reading those
  straight from their type code is what stopped the parser.
- Member names are normalised on the way in. One file spells a field `ArrayList+_size`
  and another spells the same field `ArrayList__size`.
- A class is filed under its id before its member types are known, and what is filed is
  a copy. Filling the types in afterwards changed only the local copy, so a later record
  referring back to that class read all of its members as objects, including the two
  numbers holding a layer's width and height.
- Paint.NET stores straight alpha, not premultiplied. A pixel in the test files is grey
  234 at alpha 127, which is impossible premultiplied, because a premultiplied colour is
  never brighter than its own alpha. Premultiplying measured a mean error of 149 of 255.

Blend modes are translated rather than cast, because the two enums differ in order and in
content: Additive becomes Linear Dodge, Reflect Hard Light, Negation Exclusion and XOR
Hard Mix, the closest things this program has.

**The Open button killed the program.** `openFilters()` called itself until the stack ran
out, so the file chooser never appeared. No test pressed Open, and the round trip test
opens a file by calling the loader directly, which is the whole reason it survived.

**The project format is now `.pdq`.** Paint.NET uses `.pdn` for a different format, so
one extension was two formats looking identical in a file chooser, with the difference
discovered only by trying to open the file. Paint.NET's format is read here and not
written, so a document that arrived as a `.pdn` leaves as a `.pdq` or a PNG.

**A Paint.NET file that cannot be read says so.** It used to say "illegal number", which
is the JSON parser's complaint about four bytes of a file it was never meant to see.

## 1.0.2 — 2026-10-03

A review of the code found four real problems. All four are fixed here, and one
false claim in the documentation is withdrawn.

**The `.pdn` claim was wrong.** The README, the AppStream description and the
release notes said a document moves between Paint.NET and Paint.QT in either
direction. It does not. Measured on a file this program writes: it begins with
`{` and is JSON. A Paint.NET file begins with the four bytes `PDN3`. There is no
`PDN3` anywhere in the tree. A document now has to leave Paint.QT as a PNG, or as
a `.pdn` that only Paint.QT will reopen. Reading Paint.NET's format means
implementing its container, its XML metadata and its NRBF serialisation; that
work has not been done and is not claimed to be.

**The loader accepted hostile files.** Sizes up to 200000 by 200000 passed the
old check — 160,000,000,000 bytes for one layer — and the multiplication was
done in `int`, where it wraps to 1,086,210,048, so the comparison meant to
refuse it passed instead. `qUncompress` trusted the size in the stream header.
The layer count was not limited. In `loadSelection` the mask dimensions went
straight to `QImage` unchecked.

Sizes are now computed as `qint64` and bounded at 30000 a side and 80
megapixels in total, layers are capped at 256, and the declared uncompressed
length is read off the front of the buffer and checked against the canvas before
anything is allocated. Measured with a probe outside the tree:

- 200000x200000 canvas — refused, "Invalid image dimensions"
- layer of the wrong size — refused, "The layer does not match the canvas size"
- 4000 layers — refused, "This file has more layers than this program will open"
- selection 200000x200000 — refused, "Invalid image dimensions"
- a file of our own — loaded, 800x600, 1 layer

That last line is the one that matters. A loader that refuses everything is not
a fix.

**The image decoders are bounded too.** Qt's decoders do have a ceiling, so the
obvious bomb does not work: a truncated PNG claiming 60000x60000 is refused by
Qt itself with the process peaking at 22 MB. But a file that is not truncated is
a different matter. A valid 8000x8000 PNG is 202 kB on disk and decodes to
256 MB, taking the process from 21 MB to 273 MB, and Qt accepts it. That ceiling
is per image, and `imageCount()` is a number read out of the file, so the limit
on memory was the decoder's ceiling multiplied by a count the file chose. Each
canvas is now checked against the header before it is read, and the frame count
is capped. An 8000x8000 image and a 64x64 one both still open.

**`install.sh` disabled the signature it documents.** The Flatpak path added the
remote with `--no-gpg-verify`. It now fetches the key, compares its fingerprint
against `15CC07DFA2F7AC0DA659E4B47C5A64187D334294`, refuses to continue on a
mismatch, and passes the key with `--gpg-import`. Verified both ways: the real
key installs and the app answers `Paint.QT 1.0.2`; a generated throwaway key is
rejected with both fingerprints printed.

**The AppImage toolchain is pinned.** `linuxdeploy`, `linuxdeploy-plugin-qt` and
`appimagetool` were fetched from branches named `continuous`, whose contents can
change without the URL changing. Their checksums are now pinned in the job, and a
mismatch fails the step instead of quietly changing the output.

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