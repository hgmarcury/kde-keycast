#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Marc G.
# SPDX-License-Identifier: GPL-3.0-or-later

set -euo pipefail

KPACKAGETOOL="$(command -v kpackagetool6 || true)"
KWritECONFIG="$(command -v kwriteconfig6 || true)"
QDBUS="$(command -v qdbus6 || command -v qdbus-qt6 || true)"

if [[ -z "$KPACKAGETOOL" ]]; then
    echo "ERROR: kpackagetool6 not found."
    exit 1
fi

if [[ -z "$KWritECONFIG" ]]; then
    echo "ERROR: kwriteconfig6 not found."
    exit 1
fi

if [[ -z "$QDBUS" ]]; then
    echo "ERROR: qdbus6 or qdbus-qt6 not found."
    exit 1
fi

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
        --remove kde-keycast || true
else
    echo "==> KDE KeyCast KWin script is not installed."
fi

echo "==> Reconfiguring KWin..."

"$QDBUS" \
    org.kde.KWin \
    /KWin \
    org.kde.KWin.reconfigure

echo "==> KDE KeyCast KWin integration uninstalled."
