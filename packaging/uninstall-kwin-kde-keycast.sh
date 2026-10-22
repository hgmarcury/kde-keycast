#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Marc G.
# SPDX-License-Identifier: GPL-3.0-or-later

set -euo pipefail

KPACKAGETOOL="$(command -v kpackagetool6 || true)"
KWritECONFIG="$(command -v kwriteconfig6 || true)"
QDBUS="$(command -v qdbus6 || command -v qdbus-qt6 || true)"
PKEXEC="$(command -v pkexec || true)"

for tool in KPACKAGETOOL KWritECONFIG QDBUS; do
    if [[ -z "${!tool}" ]]; then
        echo "ERROR: Required KDE tool not found: $tool"
        exit 1
    fi
done

DATA_HOME="${XDG_DATA_HOME:-"$HOME/.local/share"}"
USER_PACKAGE="$DATA_HOME/kwin/scripts/kde-keycast"
LEGACY_GLOBAL_PACKAGE="/usr/local/share/kwin/scripts/kde-keycast"

is_keycast_package() {
    local path="$1"
    local metadata="$path/metadata.json"

    [[ -d "$path" && ! -L "$path" ]] || return 1
    [[ -f "$metadata" && ! -L "$metadata" ]] || return 1

    grep -Eq '"KPackageStructure"[[:space:]]*:[[:space:]]*"KWin/Script"' \
        "$metadata" &&
    grep -Eq '"Id"[[:space:]]*:[[:space:]]*"kde-keycast"' \
        "$metadata" &&
    grep -Eq '"Name"[[:space:]]*:[[:space:]]*"KDE KeyCast"' \
        "$metadata"
}

echo "==> Uninstalling KDE KeyCast KWin integration..."

# Disable the plugin before removing its package.
"$KWritECONFIG" \
    --file kwinrc \
    --group Plugins \
    --key kde-keycastEnabled \
    --type bool \
    false \
    --notify

# Remove the user-scope installation only if its identity is verified.
if [[ -e "$USER_PACKAGE" || -L "$USER_PACKAGE" ]]; then
    if ! is_keycast_package "$USER_PACKAGE"; then
        echo "ERROR: The user package path is not a verified KDE KeyCast package:"
        echo "  $USER_PACKAGE"
        exit 1
    fi

    echo "==> Removing user-scope KDE KeyCast package..."

    "$KPACKAGETOOL" \
        --type=KWin/Script \
        --remove kde-keycast

    if [[ -e "$USER_PACKAGE" || -L "$USER_PACKAGE" ]]; then
        echo "ERROR: The user package could not be removed."
        exit 1
    fi
else
    echo "==> No user-scope package found."
fi

# Remove only the known legacy global installation, after verifying it.
if [[ -e "$LEGACY_GLOBAL_PACKAGE" || -L "$LEGACY_GLOBAL_PACKAGE" ]]; then
    if ! is_keycast_package "$LEGACY_GLOBAL_PACKAGE"; then
        echo "ERROR: The legacy global path is not a verified KDE KeyCast package:"
        echo "  $LEGACY_GLOBAL_PACKAGE"
        exit 1
    fi

    if [[ -z "$PKEXEC" ]]; then
        echo "ERROR: pkexec is required to remove the legacy global installation."
        exit 1
    fi

    echo "==> Removing verified legacy global KDE KeyCast package..."

    "$PKEXEC" /usr/bin/rm -rf -- "$LEGACY_GLOBAL_PACKAGE"

    if [[ -e "$LEGACY_GLOBAL_PACKAGE" || -L "$LEGACY_GLOBAL_PACKAGE" ]]; then
        echo "ERROR: The legacy global package still exists."
        exit 1
    fi
else
    echo "==> No legacy global package found."
fi

echo "==> Reconfiguring KWin..."

"$QDBUS" \
    org.kde.KWin \
    /KWin \
    org.kde.KWin.reconfigure

echo "==> KDE KeyCast KWin integration uninstalled."
