#!/usr/bin/env bash
# Ставит Paint.QT на этой машине.
#
#   ./install.sh            # пакет Arch: makepkg + pacman, спросит пароль
#   ./install.sh --flatpak  # через Flatpak, пароль не нужен
#
# Путь с Flatpak проверяет подпись репозитория. Ключ подписи лежит в репозитории
# и сверяется с отпечатком, который зашит в этот скрипт, до того как он будет
# передан flatpak. Если отпечаток не тот, установка прекращается.
set -euo pipefail

app=io.github.RubCut.Paint.QT
repo=https://rubcut.github.io/Paint.QT/repo
key_url=https://github.com/RubCut/Paint.QT/releases/latest/download/paint-qt.gpg
# Тот же ключ, которым подписан экспорт в .github/workflows/build.yml. Ключ и его
# резервная копия лежат у автора проекта.
key_fpr=15CC07DFA2F7AC0DA659E4B47C5A64187D334294

if [[ "${1:-}" == "--flatpak" ]]; then
    here="$(mktemp -d)"
    trap 'rm -rf "$here"' EXIT

    command -v flatpak >/dev/null || {
        echo "flatpak не найден. Установите его и повторите." >&2
        exit 1
    }

    echo "Получаю ключ подписи: $key_url"
    if command -v curl >/dev/null; then
        curl -fsSL "$key_url" -o "$here/paint-qt.gpg"
    elif command -v wget >/dev/null; then
        wget -q -O "$here/paint-qt.gpg" "$key_url"
    else
        echo "Нужен curl или wget, чтобы скачать ключ." >&2
        exit 1
    fi

    # Отпечаток сверяется здесь, а не передаётся в flatpak как есть: ключ
    # приходит по сети, и доверять ему надо не больше, чем чему-либо ещё.
    if command -v gpg >/dev/null; then
        got="$(gpg --show-keys --with-colons "$here/paint-qt.gpg" 2>/dev/null \
               | awk -F: '/^fpr/{print $10; exit}')"
        if [[ "$got" != "$key_fpr" ]]; then
            echo "Отпечаток ключа не совпал." >&2
            echo "  ожидался: $key_fpr" >&2
            echo "  получен:  ${got:-<ключ не прочитан>}" >&2
            echo "Установка прекращена. Что-то подменило ключ или сеть не доверяет." >&2
            exit 1
        fi
        echo "Отпечаток ключа совпал: $key_fpr"
    else
        echo "gpg не найден, отпечаток вручную не сверю. flatpak сверит ключ сам."
    fi

    # --gpg-import, а не --no-gpg-verify: репозиторий подписан, и выключать
    # проверку значило бы выключить то, ради чего он подписан.
    flatpak remote-add --user --if-not-exists --gpg-import "$here/paint-qt.gpg" \
        paintqt "$repo"
    flatpak install --user --noninteractive paintqt "$app"
    echo "Готово. Запуск: flatpak run $app"
    exit 0
fi

command -v makepkg >/dev/null || {
    echo "Этот путь ставит пакет Arch и требует makepkg и pacman." >&2
    echo "На другом дистрибутиве соберите из исходников или используйте:" >&2
    echo "    ./install.sh --flatpak" >&2
    exit 1
}

here="$(mktemp -d)"
trap 'rm -rf "$here"' EXIT
cp "$(dirname "${BASH_SOURCE[0]}")/packaging/aur/PKGBUILD" "$here/"
cd "$here"

# makepkg сам качает архив тега и проверяет сумму из PKGBUILD.
makepkg -s --noconfirm --needed
sudo pacman -U --noconfirm ./*.pkg.tar.zst

echo "Готово. Запуск: paint-qt"
