// Package audit.
//
// Every file the packaging reads is checked here, because a release that nobody
// looked at before shipping is the failure mode: a .desktop entry naming a
// format the binary cannot open, a metainfo file Flathub rejects, an icon that
// is not where the .desktop file says it is, or a PKGBUILD that references a
// source it does not download.
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileInfo>
#include <QImage>
#include <QSet>
#include <QXmlStreamReader>

#include <algorithm>
#include <cstdio>

static int g_failures = 0;

static void check(bool ok, const QString& what)
{
    std::printf("  [%s] %s\n", ok ? " OK " : "FAIL", qPrintable(what));
    if (!ok)
        ++g_failures;
}

/// Reads a whole file, or an empty string when it is not there.
static QString readAll(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return QString();
    return QString::fromUtf8(f.readAll());
}

/// The value of a top level `key: value` line, or an empty string.
///
/// The manifest is small and read for auditing rather than for building, so a
/// line scan is enough and avoids a YAML dependency in the test.
static QString valueOf(const QString& text, const QString& key)
{
    for (const QString& l : text.split(QLatin1Char('\n'))) {
        const QString t = l.trimmed();
        if (!t.startsWith(key))
            continue;
        QString v = t.mid(key.size()).trimmed();
        if (v.startsWith(QLatin1Char('\'')) || v.startsWith(QLatin1Char('"')))
            v = v.mid(1);
        if (v.endsWith(QLatin1Char('\'')) || v.endsWith(QLatin1Char('"')))
            v.chop(1);
        return v;
    }
    return QString();
}

/// Strips comments and blank lines from a shell style file.
static QStringList codeLines(const QString& text)
{
    QStringList out;
    const QStringList lines = text.split(QLatin1Char('\n'));
    for (const QString& l : lines) {
        const QString t = l.trimmed();
        if (t.isEmpty() || t.startsWith(QLatin1Char('#')))
            continue;
        out << t;
    }
    return out;
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    const QString root = QStringLiteral(PNQ_SOURCE_DIR);
    const QString pkg = root + QStringLiteral("/packaging");
    std::printf("=== package audit ===\n");

    // ------------------------------------------------------------- desktop file
    {
        const QString path = pkg + QStringLiteral("/linux/paint-qt.desktop");
        const QString text = readAll(path);
        check(!text.isEmpty(), QStringLiteral("packaging/linux/paint-qt.desktop exists"));
        if (text.isEmpty())
            return 1;

        const QStringList lines = text.split(QLatin1Char('\n'));
        check(lines.value(0).trimmed() == QLatin1String("[Desktop Entry]"),
              QStringLiteral("desktop entry group comes first"));

        // Every key exactly once, and the mandatory ones present.
        QStringList keys;
        bool malformed = false;
        for (int i = 1; i < lines.size(); ++i) {
            const QString t = lines.at(i).trimmed();
            if (t.isEmpty() || t.startsWith(QLatin1Char('#')))
                continue;
            // A group header starts with '[' and is not a key.
            if (t.startsWith(QLatin1Char('['))) {
                if (!t.endsWith(QLatin1Char(']')) || t != QLatin1String("[Desktop Entry]"))
                    malformed = true;
                continue;
            }
            if (!t.contains(QLatin1Char('='))) {
                malformed = true;
                continue;
            }
            keys << t.section(QLatin1Char('='), 0, 0);
        }
        check(!malformed, QStringLiteral("every desktop line is a key=value pair"));
        check(keys.count() == QSet<QString>(keys.begin(), keys.end()).size(),
              QStringLiteral("no duplicate keys in the desktop entry"));
        for (const QString& required : { QStringLiteral("Type"), QStringLiteral("Name"),
                                         QStringLiteral("Exec"), QStringLiteral("Icon"),
                                         QStringLiteral("Categories") })
            check(keys.contains(required),
                  QStringLiteral("desktop entry has %1").arg(required));

        // Exec has to name the binary the package installs, and %f has to be
        // there or the file manager will not pass the document along.
        QString exec;
        QString icon;
        for (const QString& raw : lines) {
            if (raw.startsWith(QLatin1String("Exec=")))
                exec = raw.section(QLatin1Char('='), 1);
            if (raw.startsWith(QLatin1String("Icon=")))
                icon = raw.section(QLatin1Char('='), 1);
        }
        check(exec.startsWith(QLatin1String("paint-qt")),
              QStringLiteral("Exec runs paint-qt (%1)").arg(exec));
        check(exec.contains(QLatin1String("%f")),
              QStringLiteral("Exec passes the file to open (%1)").arg(exec));
        check(icon == QLatin1String("paint-qt"),
              QStringLiteral("Icon is paint-qt, matching the installed hicolor name (%1)")
                  .arg(icon));

        // The declared MIME types have to be the formats the program can read.
        // A desktop entry claiming more than the binary opens is a double click
        // that does nothing.
        QStringList mime;
        for (const QString& raw : lines)
            if (raw.startsWith(QLatin1String("MimeType=")))
                mime = raw.section(QLatin1Char('='), 1).split(QLatin1Char(';'), Qt::SkipEmptyParts);
        check(mime.size() >= 10,
              QStringLiteral("the desktop entry declares its image formats (%1)").arg(mime.size()));
        for (const QString& required : { QStringLiteral("image/png"), QStringLiteral("image/jpeg"),
                                         QStringLiteral("image/bmp") })
            check(mime.contains(required),
                  QStringLiteral("MimeType declares %1").arg(required));
    }

    // ------------------------------------------------------------- appstream
    {
        const QString path =
            pkg + QStringLiteral("/linux/io.github.paintqt.Paint.QT.metainfo.xml");
        const QString text = readAll(path);
        check(!text.isEmpty(),
              QStringLiteral("AppStream metainfo exists, without which Flathub rejects a submission"));
        if (!text.isEmpty()) {
            // Flathub requires a component with an id, a licence, a content
            // rating and at least one release.
            QString id, name, summary, metadataLicense, projectLicense, launchable;
            int releases = 0;
            QXmlStreamReader xml(text);
            while (!xml.atEnd()) {
                xml.readNext();
                if (!xml.isStartElement())
                    continue;
                const QString e = xml.name().toString();
                if (e == QLatin1String("release")) {
                    ++releases;
                    continue;
                }
                // readElementText() fails on an element that has child elements,
                // reporting "Expected character data", which would be a complaint
                // about the test rather than about the file. Peek at the next
                // token instead and skip anything that opens a child.
                xml.readNext();
                if (xml.tokenType() == QXmlStreamReader::StartElement)
                    continue;
                xml.readElementText();
                const QString v = xml.text().toString().trimmed();
                if (e == QLatin1String("id")) id = v;
                // <name> also names the developer. The component's own <name>
                // comes first in the document, so the first one wins.
                else if (e == QLatin1String("name") && name.isEmpty()) name = v;
                else if (e == QLatin1String("summary")) summary = v;
                else if (e == QLatin1String("metadata_license")) metadataLicense = v;
                else if (e == QLatin1String("project_license")) projectLicense = v;
                else if (e == QLatin1String("launchable")) launchable = v;
            }
            check(!xml.hasError(),
                  QStringLiteral("metainfo is well formed XML (%1)")
                      .arg(xml.hasError() ? xml.errorString() : QStringLiteral("no error")));
            check(id == QLatin1String("io.github.paintqt.Paint.QT"),
                  QStringLiteral("metainfo id is the application id (%1)").arg(id));
            check(name == QLatin1String("Paint.QT"),
                  QStringLiteral("metainfo name is Paint.QT (%1)").arg(name));
            check(!summary.isEmpty(), QStringLiteral("metainfo has a summary"));
            check(metadataLicense == QLatin1String("CC0-1.0"),
                  QStringLiteral("metadata_license is CC0-1.0 (%1)").arg(metadataLicense));
            check(projectLicense == QLatin1String("GPL-3.0-or-later"),
                  QStringLiteral("project_license matches the shipped LICENSE (%1)")
                      .arg(projectLicense));
            check(launchable.endsWith(QLatin1String(".desktop")),
                  QStringLiteral("launchable names a desktop file (%1)").arg(launchable));
            check(releases >= 1,
                  QStringLiteral("metainfo lists at least one release (%1)").arg(releases));

            // Flathub requires screenshots, and appstreamcli rejects a relative
            // path with "web-url-expected": the <image> has to be a direct URL.
            // This is reproduced here because a validation error fails the whole
            // Flatpak build at the compose step.
            QXmlStreamReader shots;
            shots.addData(text.toUtf8());
            QStringList shotUrls;
            while (!shots.atEnd()) {
                shots.readNext();
                if (shots.name().toString() == QLatin1String("image")) {
                    const QString v = shots.readElementText().trimmed();
                    if (v.startsWith(QLatin1String("http")))
                        shotUrls << v;
                    else if (v.endsWith(QLatin1String(".png")))
                        shotUrls << QStringLiteral("<relative:%1>").arg(v);
                }
            }
            check(!shotUrls.isEmpty(),
                  QStringLiteral("metainfo lists screenshots, which Flathub requires (%1)")
                      .arg(shotUrls.size()));
            QStringList badShots;
            for (const QString& u : std::as_const(shotUrls))
                if (!u.startsWith(QLatin1String("https://")))
                    badShots << u;
            check(badShots.isEmpty(),
                  badShots.isEmpty()
                      ? QStringLiteral("every screenshot is an https URL")
                      : QStringLiteral("screenshots that are not https URLs: %1")
                            .arg(badShots.join(QStringLiteral(", "))));
            // The same control must not appear under both <supports> and
            // <recommends>; appstreamcli calls that a validation error.
            const bool bothRelations = text.contains(QLatin1String("<recommends>"))
                                       && text.contains(QLatin1String("<supports>"));
            check(!bothRelations,
                  QStringLiteral("controls are not declared under both supports and recommends"));
            check(QFile::exists(root + QStringLiteral("/LICENSE")),
                  QStringLiteral("a LICENSE file ships, which the licence claim depends on"));
            const QString licence = readAll(root + QStringLiteral("/LICENSE"));
            check(licence.contains(QLatin1String("GNU GENERAL PUBLIC LICENSE")),
                  QStringLiteral("the shipped LICENSE is the GPL"));
        }
    }

    // ------------------------------------------------------------------ icons
    {
        const QString dir = pkg + QStringLiteral("/icons");
        const QString svg = readAll(dir + QStringLiteral("/paint-qt.svg"));
        check(!svg.isEmpty(), QStringLiteral("packaging/icons/paint-qt.svg exists"));
        check(svg.contains(QLatin1String("<svg")) && svg.contains(QLatin1String("viewBox")),
              QStringLiteral("the icon is an SVG with a viewBox"));

        // The prebuilt PNGs and the .ico ship in the repository, because the
        // flatpak manifest and the PKGBUILD both build from a tarball where
        // running the generator is not always possible.
        int sizesFound = 0;
        for (const int s : { 16, 24, 32, 48, 64, 128, 256, 512 }) {
            const QString p =
                QStringLiteral("%1/paint-qt-%2.png").arg(dir).arg(s);
            QImage img(p);
            if (img.isNull() || img.width() != s || img.height() != s)
                continue;
            ++sizesFound;
        }
        check(sizesFound >= 6,
              QStringLiteral("the shipped PNGs are present at the right sizes (%1 of 8)")
                  .arg(sizesFound));

        // A .ico has to start with the ICO header, not a PNG signature: a PNG
        // named .ico is what a hand rolled packager produces.
        QFile ico(dir + QStringLiteral("/paint-qt.ico"));
        check(ico.open(QIODevice::ReadOnly), QStringLiteral("paint-qt.ico exists"));
        if (ico.isOpen()) {
            const QByteArray head = ico.read(6);
            const bool isIco = head.size() == 6 && head[0] == 0 && head[1] == 0
                               && head[2] == char(1) && head[3] == 0;
            check(isIco, QStringLiteral("paint-qt.ico carries an ICO header, not a bare PNG"));
            ico.seek(0);
            const QByteArray all = ico.readAll();
            const quint32 declared = quint32(uchar(head.size() > 4 ? head[4] : 0))
                                     | (quint32(uchar(head.size() > 5 ? head[5] : 0)) << 8);
            check(declared > 0 && quint32(all.size()) >= 6 + 16 * declared,
                  QStringLiteral("the .ico directory fits inside the file (%1 entries)")
                      .arg(declared));
        }
    }

    // ------------------------------------------------------------- flatpak
    {
        // The extension is not cosmetic: flatpak-builder picks its parser from
        // it, so a .json manifest has to be JSON and this one is YAML.
        const QString path = pkg + QStringLiteral("/flatpak/io.github.paintqt.Paint.QT.yaml");
        const QString text = readAll(path);
        check(!text.isEmpty(), QStringLiteral("the flatpak manifest exists as YAML"));
        check(!QFile::exists(pkg + QStringLiteral("/flatpak/io.github.paintqt.Paint.QT.json")),
              QStringLiteral("no .json manifest, which flatpak-builder would parse as JSON"));
        if (!text.isEmpty()) {
            check(text.contains(QLatin1String("id: io.github.paintqt.Paint.QT")),
                  QStringLiteral("the manifest id matches the desktop entry"));
            // The runtime family is deliberately not pinned here. A rolling
            // runtime such as org.gnome.Platform has no version line, and a
            // numbered one such as org.kde.Platform has to be bumped by hand
            // when Flathub retires the version: the build failed with "Unable to
            // find sdk org.kde.Sdk version 6.7" for exactly that reason. What
            // has to hold is that runtime and sdk come from the same family and
            // that both are declared, since the workflow installs them by name.
            const QString runtime = valueOf(text, QStringLiteral("runtime:"));
            const QString sdk = valueOf(text, QStringLiteral("sdk:"));
            check(!runtime.isEmpty(), QStringLiteral("the manifest names a runtime"));
            check(!sdk.isEmpty(), QStringLiteral("the manifest names an sdk"));
            // The sdk of a family is the runtime name without its .Platform
            // suffix: org.gnome.Platform pairs with org.gnome.Sdk, and pairing
            // across families builds against the wrong headers.
            const QString family = runtime.endsWith(QLatin1String(".Platform"))
                                       ? runtime.left(runtime.size() - 9)
                                       : runtime;
            check(!runtime.isEmpty() && !sdk.isEmpty() && sdk == family + QLatin1String(".Sdk"),
                  !sdk.isEmpty() && sdk == family + QLatin1String(".Sdk")
                      ? QStringLiteral("runtime and sdk come from one family (%1)").arg(family)
                      : QStringLiteral("sdk %1 does not belong to runtime family %2")
                            .arg(sdk, family));
            // The workflow installs the runtimes by name, so the two files have to
            // agree. The build failed with "Unable to find sdk org.kde.Sdk
            // version 6.7" because the manifest and the workflow named different
            // things; this compares the two rather than trusting either.
            const QString version = valueOf(text, QStringLiteral("runtime-version:"));
            check(!version.isEmpty(),
                  QStringLiteral("the manifest pins a runtime version (%1)").arg(version));
            const QString workflow =
                readAll(root + QStringLiteral("/.github/workflows/build.yml"));
            if (!workflow.isEmpty() && !runtime.isEmpty() && !version.isEmpty()) {
                // The workflow installs these by name, and the build fails with
                // "Nothing matches org.kde.Sdk in remote flathub" when the two
                // files disagree, so they are compared rather than assumed.
                const QString rt = runtime + QLatin1String("//") + version;
                const QString sk = sdk + QLatin1String("//") + version;
                check(workflow.contains(rt),
                      QStringLiteral("CI installs the runtime the manifest names (%1)").arg(rt));
                check(workflow.contains(sk),
                      QStringLiteral("CI installs the sdk the manifest names (%1)").arg(sk));
                // The flatpakref tells software centres which runtime to fetch, so
                // it has to name the same one or the install fails at the user.
                check(workflow.contains(QStringLiteral("/x86_64/") + version),
                      QStringLiteral("the flatpakref names runtime version %1").arg(version));
            }
            // `base: app` is an invalid application id: an app base name needs
            // at least two periods. With runtime and sdk given, base is derived
            // and must be absent.
            check(!text.contains(QLatin1String("base: app")),
                  QStringLiteral("the manifest has no invalid 'base: app' line"));
            check(text.contains(QLatin1String("command: paint-qt")),
                  QStringLiteral("the manifest launches paint-qt"));
            // The application id rules, checked here because they are cheap and
            // a violation means the app has to be resubmitted under a new id.
            const QString id = QStringLiteral("io.github.paintqt.Paint.QT");
            check(text.contains(QStringLiteral("id: ") + id),
                  QStringLiteral("the manifest declares the id %1").arg(id));
            check(id.startsWith(QLatin1String("io.github.")),
                  QStringLiteral("a code hosted app id must start with io.github."));
            check(id.count(QLatin1Char('.')) >= 3,
                  QStringLiteral("a code hosted app id needs at least four components (%1)")
                      .arg(id));
            check(!text.contains(QLatin1String("base: app")),
                  QStringLiteral("no invalid 'base: app' line"));
            // Building with network access is refused by Flathub.
            check(!text.contains(QLatin1String("--share=network")),
                  QStringLiteral("the build does not ask for network access"));
            // A Flatpak that runs outside its sandbox needs a reason. The
            // permission list is short on purpose and should stay that way.
            const QStringList args = codeLines(text);
            QStringList permissions;
            bool inArgs = false;
            for (const QString& l : args) {
                if (l.startsWith(QLatin1String("finish-args:")))
                    inArgs = true;
                else if (inArgs && l.startsWith(QLatin1String("- --")))
                    permissions << l;
                else if (inArgs && !l.startsWith(QLatin1String("- --")))
                    inArgs = false;
            }
            check(permissions.size() <= 8,
                  QStringLiteral("the sandbox asks for few permissions (%1)")
                      .arg(permissions.size()));
            for (const QString& p : permissions) {
                check(!p.contains(QLatin1String("--talk-name="))
                          && !p.contains(QLatin1String("--system-talk-name=")),
                      QStringLiteral("no broad D-Bus access requested (%1)").arg(p.trimmed()));
            }
        }
        check(QFileInfo(pkg + QStringLiteral("/flatpak/build-flatpak.sh")).isExecutable(),
              QStringLiteral("the flatpak build script is executable"));
    }

    // ----------------------------------------------------------------- aur
    {
        const QString path = pkg + QStringLiteral("/aur/PKGBUILD");
        const QString text = readAll(path);
        check(!text.isEmpty(), QStringLiteral("packaging/aur/PKGBUILD exists"));
        if (!text.isEmpty()) {
            check(text.contains(QLatin1String("pkgname=paint-qt")),
                  QStringLiteral("PKGBUILD names the package paint-qt"));
            // The paths are variables, so look for the commands rather than for
            // one particular spelling of the build directory.
            check(text.contains(QLatin1String("cmake --build")),
                  QStringLiteral("PKGBUILD builds the project"));
            check(text.contains(QLatin1String("cmake --install")),
                  QStringLiteral("PKGBUILD installs the project"));
            // CMake reuses whatever prefix the build tree was configured with, so
            // the install line has to name /usr itself. Without it every file
            // lands in /usr/local, which is wrong on Arch.
            check(text.contains(QLatin1String("--prefix /usr")),
                  QStringLiteral("PKGBUILD installs into /usr rather than /usr/local"));
            // The build tree has to be addressed by absolute path: makepkg runs
            // each step in the package directory, and a relative `build` path
            // breaks as soon as a step changes directory.
            check(text.contains(QLatin1String("_builddir=")),
                  QStringLiteral("PKGBUILD addresses the build tree by absolute path"));
            check(text.contains(QLatin1String("-B \"$_builddir\"")),
                  QStringLiteral("PKGBUILD configures into that absolute path"));
            // The install step has to place the desktop entry, the metadata and
            // the licence by hand, because CPack is not what makepkg runs.
            for (const QString& need : { QStringLiteral("paint-qt.desktop"),
                                         QStringLiteral(".metainfo.xml"),
                                         QStringLiteral("LICENSE") })
                check(text.contains(need),
                      QStringLiteral("PKGBUILD installs %1").arg(need));
            check(QFile::exists(pkg + QStringLiteral("/aur/.SRCINFO")),
                  QStringLiteral(".SRCINFO is committed, which AUR requires"));
            // Every source has a checksum line, or makepkg refuses the build.
            const QStringList lines = codeLines(text);
            const int sources = int(std::count_if(lines.cbegin(), lines.cend(),
                                                  [](const QString& l) {
                                                      return l.startsWith(QLatin1String("source="));
                                                  }));
            const int sums = int(std::count_if(lines.cbegin(), lines.cend(),
                                               [](const QString& l) {
                                                   return l.startsWith(QLatin1String("sha256sums="));
                                               }));
            check(sources > 0 && sources == sums,
                  QStringLiteral("every source has a checksum (%1 sources, %2 sums)")
                      .arg(sources).arg(sums));
        }
    }

    // ------------------------------------------------------- version, once
    {
        // The About box and every package read the same generated header, so the
        // version cannot drift between them. Confirm the template exists and
        // that nothing hardcodes a version in the sources.
        check(QFile::exists(pkg + QStringLiteral("/paint-qt.version.in")),
              QStringLiteral("the version template exists"));
        const QString main = readAll(root + QStringLiteral("/src/main.cpp"));
        check(!main.contains(QLatin1String("setApplicationVersion(QStringLiteral(")),
              QStringLiteral("main.cpp takes its version from the generated header"));
        check(main.contains(QLatin1String("PNQ_APP_ID")),
              QStringLiteral("main.cpp sets the desktop file name from the application id"));
    }

    std::printf("=== %s (%d failures) ===\n", g_failures == 0 ? "ALL PASSED" : "FAILURES",
                g_failures);
    return g_failures == 0 ? 0 : 1;
}