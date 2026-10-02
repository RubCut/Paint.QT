#!/usr/bin/env bash
# Builds the Flatpak into a local repository and, on request, installs and starts it.
#
# The manifest uses `type: dir`, so this must be run from a checkout rather than
# from a bare repository:
#
#   packaging/flatpak/build-flatpak.sh            # build into packaging/flatpak/flatpak-repo
#   packaging/flatpak/build-flatpak.sh --verify   # also install it and start it once
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
root="$(cd "${here}/../.." && pwd)"
app_id="io.github.RubCut.Paint.QT"
branch="${APP_BRANCH:-stable}"

for tool in flatpak flatpak-builder; do
    if ! command -v "${tool}" > /dev/null; then
        echo "error: ${tool} is not installed." >&2
        echo "  Flatpak:   https://flatpak.org/setup" >&2
        echo "  Builder:   https://docs.flatpak.org/en/latest/flatpak-builder.html" >&2
        exit 1
    fi
done

# The manifest's extension decides how it is parsed: flatpak-builder reads a
# .json manifest as JSON and a .yaml one as YAML. This one is YAML.
manifest="${here}/${app_id}.yaml"

# The runtime is read from the manifest rather than repeated here. It used to say
# org.kde.Platform//6.7 in both files, and when Flathub retired 6.7 the two drifted
# into disagreeing about which runtime the build wanted.
runtime="$(sed -n 's/^runtime: *//p' "${manifest}" | head -1 | tr -d "'\"")"
runtime_version="$(sed -n 's/^runtime-version: *//p' "${manifest}" | head -1 | tr -d "'\"")"
sdk="$(sed -n 's/^sdk: *//p' "${manifest}" | head -1 | tr -d "'\"")"
if [ -z "${runtime}" ] || [ -z "${sdk}" ]; then
    echo "error: could not read runtime and sdk from ${manifest}" >&2
    exit 1
fi
# A manifest may name a rolling runtime with no version line at all.
runtime_ref="${runtime}"
if [ -n "${runtime_version}" ]; then
    runtime_ref="${runtime}//${runtime_version}"
fi

echo "==> checking that Flathub still publishes ${runtime_ref}"
flatpak remote-add --user --if-not-exists flathub \
    https://dl.flathub.org/repo/flathub.flatpakrepo > /dev/null 2>&1 || true
# Asked of the remote rather than discovered by failing later: Flathub retires
# runtime versions, and a manifest naming a retired one otherwise fails deep
# inside the build with "Unable to find sdk ... version ...".
published="$(flatpak remote-ls --user flathub --arch=x86_64 --columns=application,branch 2>/dev/null || true)"
if ! awk -F'\t' -v r="${runtime}" -v v="${runtime_version}" \
        '$1 == r && $2 == v { found = 1 } END { exit !found }' <<< "${published}"; then
    echo "error: Flathub no longer publishes ${runtime} version ${runtime_version}." >&2
    echo "versions it does publish for ${runtime}:" >&2
    awk -F'\t' -v r="${runtime}" '$1 == r { print "  " $2 }' <<< "${published}" \
        | sort -V | tail -8 >&2
    exit 1
fi

echo "==> adding ${runtime_ref} if it is missing"
flatpak install --user --if-not-exists -y flathub "${runtime_ref}" \
    "${sdk}//${runtime_version:-master}"

# flatpak-builder takes BUILD_DIR before MANIFEST. Passing the branch first, as
# this script used to, made the branch the build directory and the manifest a
# second positional, and the build landed in a directory called "stable".
build_dir="${here}/flatpak-build"
repo_dir="${here}/flatpak-repo"

echo "==> building ${app_id}//${branch}"
rm -rf "${build_dir}" "${repo_dir}"
cd "${root}"
flatpak-builder --user --repo="${repo_dir}" --force-clean "${build_dir}" "${manifest}"

if [[ "${1:-}" == "--verify" ]]; then
    # Installed and started, rather than flatpak-builder --verify, which the
    # distribution's flatpak-builder does not have: it answers "Option parsing
    # failed: Unknown option --verify".
    #
    # QT_QPA_PLATFORM=offscreen because there is normally no display here. The
    # runtime carries the offscreen platform plugin, which a packaged AppImage
    # built by linuxdeploy does not.
    echo "==> installing and starting it once"
    flatpak remote-add --user --if-not-exists paintqt-local "file://${repo_dir}"
    flatpak install --user -y --no-deps paintqt-local "${app_id}"
    QT_QPA_PLATFORM=offscreen flatpak run --user "${app_id}" --version
fi

echo
echo "Built into ${repo_dir}."
echo "Install it with:"
echo "  flatpak remote-add --user --if-not-exists paintqt-local file://${repo_dir}"
echo "  flatpak install --user paintqt-local ${app_id}"
