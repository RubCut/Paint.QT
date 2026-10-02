# Security policy

## Reporting a vulnerability

Please report it privately, through
[GitHub's security advisory form](https://github.com/RubCut/Paint.QT/security/advisories/new).
Do not open a public issue for a vulnerability: an issue is visible immediately and
gives whoever reads it a working description before a fix exists.

What helps most in the report:

- what an attacker can do, and what they need in order to do it
- the version, and how it was installed
- a `.pdn` file or an image that reproduces it, if one is needed
- the steps, in order

## What to expect

An acknowledgement, then a fix, then a release and a note in
[CHANGELOG.md](CHANGELOG.md). If a report turns out not to be a vulnerability, that
will be said plainly and the discussion will move to an issue.

## What this program opens

A raster image editor reads files written by other programs, so a malformed image is
the obvious place for a fault. The parsers are the interesting surface:

| Format | Handled by | Notes |
| --- | --- | --- |
| `.pdn` | `src/io/PdnFile.cpp` | The format this program also writes, so it parses its own output |
| PNG, JPEG, BMP, TIFF, WebP, GIF | Qt's image plugins, listed in `src/io/FileFormats.cpp` | Anything Qt 6.2 can decode |

PSD is listed as a MIME type in the desktop entry for the benefit of file managers,
but there is no PSD reader in the code, so a `.psd` does not open.

The Flatpak requests no privileges beyond the defaults plus the picture, document and
download directories. It asks for no network access and no access outside those
directories.

## Signing

The Flatpak repository is signed with one long lived key,
`15CC07DFA2F7AC0DA659E4B47C5A64187D334294`. A release whose signature does not
verify was not produced by this project, whatever else it claims to be.