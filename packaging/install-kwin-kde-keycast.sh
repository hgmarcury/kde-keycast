#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Marc G.
# SPDX-License-Identifier: GPL-3.0-or-later

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

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

KWIN_PACKAGE=""

# Installed application
INSTALLED_KWIN_PACKAGE="$SCRIPT_DIR/../share/kde-keycast/kwin/kde-keycast"

# Development tree
SOURCE_KWIN_PACKAGE="$PROJECT_ROOT/kwin/kde-keycast"

if [[ -d "$INSTALLED_KWIN_PACKAGE" ]]; then
    KWIN_PACKAGE="$INSTALLED_KWIN_PACKAGE"
elif [[ -d "$SOURCE_KWIN_PACKAGE" ]]; then
    KWIN_PACKAGE="$SOURCE_KWIN_PACKAGE"
else
    echo "ERROR: KDE KeyCast KWin package not found."
    echo
    echo "Searched:"
    echo "  $INSTALLED_KWIN_PACKAGE"
    echo "  $SOURCE_KWIN_PACKAGE"
    exit 1
fi

echo "==> Installing KDE KeyCast KWin integration..."
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
