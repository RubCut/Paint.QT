#!/usr/bin/env bash
# Builds the Flatpak and verifies it.
#
# The manifest uses `type: dir`, so it must be built from a checkout rather than
# from a bare repository: run this in the root of a clone.
#
#   packaging/flatpak/build-flatpak.sh            # build the single branch
#   packaging/flatpak/build-flatpak.sh --verify   # also run flatpak-builder --verify
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
root="$(cd "${here}/../.." && pwd)"
app_id="io.github.paintqt.Paint.QT"
branch="${APP_BRANCH:-stable}"

for tool in flatpak flatpak-builder; do
    if ! command -v "${tool}" > /dev/null; then
        echo "error: ${tool} is not installed." >&2
        echo "  Flatpak:   https://flatpak.org/setup" >&2
        exit 1
    fi
done

# The manifest lives outside the repository root and refers to it with
# `type: dir`, so it is copied next to the build tree for flatpak-builder.
# The extension decides the parser: flatpak-builder reads a .json manifest as
# JSON and a .yaml one as YAML. This manifest is YAML.
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

echo "==> adding ${runtime_ref} if it is missing"
flatpak remote-add --if-not-exists flathub \
    https://dl.flathub.org/repo/flathub.flatpakrepo > /dev/null 2>&1 || true
flatpak install --if-not-exists -y flathub "${runtime_ref}" "${sdk}//${runtime_version:-master}"

# flatpak-builder resolves a relative `sources.path` against the directory it is
# run from, not against the manifest. The manifest is run from the repository
# root so that its `../..` lands on the checkout it names.
build_dir="${here}/flatpak-build"
repo_dir="${here}/flatpak-repo"

echo "==> building ${app_id}//${branch}"
rm -rf "${build_dir}" "${repo_dir}"
cd "${root}"
flatpak-builder --repo="${repo_dir}" --force-clean "${branch}" "${manifest}"

if [[ "${1:-}" == "--verify" ]]; then
    echo "==> verifying the build"
    flatpak-builder --verify --print-repo "${repo_dir}" "${branch}"
fi

echo
echo "Built into ${repo_dir}."
echo "Install it with:"
echo "  flatpak-builder --user --install --force-clean ${branch} ${manifest}"