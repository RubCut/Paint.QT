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
out="${here}/flatpak-repo"

for tool in flatpak flatpak-builder; do
    if ! command -v "${tool}" > /dev/null; then
        echo "error: ${tool} is not installed." >&2
        echo "  Flatpak:   https://flatpak.org/setup" >&2
        exit 1
    fi
done

# The manifest lives outside the repository root and refers to it with
# `type: dir`, so it is copied next to the build tree for flatpak-builder.
manifest="${here}/${app_id}.json"

echo "==> adding the KDE runtime if it is missing"
flatpak remote-add --if-not-exists flathub \
    https://dl.flathub.org/repo/flathub.flatpakrepo > /dev/null 2>&1 || true
flatpak install --if-not-exists -y flathub org.kde.Platform//6.7 org.kde.Sdk//6.7

echo "==> building ${app_id}//${branch}"
rm -rf "${out}"
flatpak-builder --repo="${out}" --force-clean "${branch}" "${manifest}"

if [[ "${1:-}" == "--verify" ]]; then
    echo "==> verifying the build"
    flatpak-builder --verify --print-repo "${out}" "${branch}"
fi

echo
echo "Built into ${out}."
echo "Install it with:"
echo "  flatpak-builder --user --install --force-clean ${branch} ${manifest}"