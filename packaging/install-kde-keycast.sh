#!/usr/bin/env bash

set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

KPACKAGETOOL="$(command -v kpackagetool6 || true)"
KWritECONFIG="$(command -v kwriteconfig6 || true)"
QDBUS="$(command -v qdbus6 || true)"

if [[ -z "$KPACKAGETOOL" ]]; then
    echo "ERROR: kpackagetool6 not found."
    exit 1
fi

if [[ -z "$KWritECONFIG" ]]; then
    echo "ERROR: kwriteconfig6 not found."
    exit 1
fi

if [[ -z "$QDBUS" ]]; then
    echo "ERROR: qdbus6 not found."
    exit 1
fi

if [[ "${1:-}" == "--uninstall" ]]; then
    echo "==> Uninstalling KDE KeyCast KWin integration..."

    if "$KPACKAGETOOL" \
        --type=KWin/Script \
        --show \
        kde-keycast >/dev/null 2>&1
    then
        echo "==> Disabling KDE KeyCast KWin script..."

        "$KWritECONFIG" \
            --file kwinrc \
            --group Plugins \
            --key kde-keycastEnabled \
            --type bool \
            false \
            --notify

        echo "==> Removing KDE KeyCast KWin script..."

        "$KPACKAGETOOL" \
            --type=KWin/Script \
            --remove \
            kde-keycast
    else
        echo "==> KDE KeyCast KWin script is not installed."
    fi

    echo "==> Reconfiguring KWin..."

    "$QDBUS" \
        org.kde.KWin \
        /KWin \
        org.kde.KWin.reconfigure

    echo "==> KDE KeyCast KWin integration uninstalled."

    exit 0
fi

echo "==> Installing KDE KeyCast KWin integration..."

if [[ $# -ge 1 ]]; then
    KWIN_PACKAGE="$1"
else
    KWIN_PACKAGE="$PROJECT_ROOT/kwin/kde-keycast"
fi

if [[ ! -d "$KWIN_PACKAGE" ]]; then
    echo "ERROR: KWin package not found:"
    echo "  $KWIN_PACKAGE"
    exit 1
fi

echo "KWin package:"
echo "  $KWIN_PACKAGE"

if "$KPACKAGETOOL" \
    --type=KWin/Script \
    --show \
    kde-keycast >/dev/null 2>&1
then
    echo "==> KDE KeyCast KWin script already installed. Updating..."

    "$KPACKAGETOOL" \
        --type=KWin/Script \
        --upgrade \
        "$KWIN_PACKAGE"
else
    echo "==> KDE KeyCast KWin script not installed. Installing..."

    "$KPACKAGETOOL" \
        --type=KWin/Script \
        --install \
        "$KWIN_PACKAGE"
fi

echo "==> Enabling KDE KeyCast KWin script..."

"$KWritECONFIG" \
    --file kwinrc \
    --group Plugins \
    --key kde-keycastEnabled \
    --type bool \
    true \
    --notify

echo "==> Reconfiguring KWin..."

"$QDBUS" \
    org.kde.KWin \
    /KWin \
    org.kde.KWin.reconfigure

echo "==> KDE KeyCast KWin integration installed."
