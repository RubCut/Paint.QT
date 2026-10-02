#!/usr/bin/env bash
# Ставит Paint.QT на этой машине: собирает Arch-пакет из выпущенного тарбола
# и ставит его. Пароль спросит pacman, он у меня недоступен.
#
#   ./install.sh            # пакет Arch
#   ./install.sh --flatpak  # через Flatpak, пароль не нужен вовсе
set -euo pipefail

app=io.github.RubCut.Paint.QT
repo=https://rubcut.github.io/Paint.QT/repo

if [[ "${1:-}" == "--flatpak" ]]; then
    flatpak remote-add --user --if-not-exists --no-gpg-verify \
        paintqt "$repo"
    flatpak install --user --noninteractive paintqt "$app"
    echo "Готово. Запуск: flatpak run $app"
    exit 0
fi

here="$(mktemp -d)"
trap 'rm -rf "$here"' EXIT
cp "$(dirname "${BASH_SOURCE[0]}")/packaging/aur/PKGBUILD" "$here/"
cd "$here"

# makepkg сам качает архив тега и проверяет сумму из PKGBUILD.
makepkg -s --noconfirm --needed
sudo pacman -U --noconfirm ./*.pkg.tar.zst

echo "Готово. Запуск: paint-qt"
