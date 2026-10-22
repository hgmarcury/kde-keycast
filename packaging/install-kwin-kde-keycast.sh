#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Marc G.
# SPDX-License-Identifier: GPL-3.0-or-later

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

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

INSTALLED_KWIN_PACKAGE="$SCRIPT_DIR/../share/kde-keycast/kwin/kde-keycast"
SOURCE_KWIN_PACKAGE="$PROJECT_ROOT/kwin/kde-keycast"

if [[ -d "$INSTALLED_KWIN_PACKAGE" ]]; then
    KWIN_PACKAGE="$INSTALLED_KWIN_PACKAGE"
elif [[ -d "$SOURCE_KWIN_PACKAGE" ]]; then
    KWIN_PACKAGE="$SOURCE_KWIN_PACKAGE"
else
    echo "ERROR: KDE KeyCast KWin package not found."
    echo "Searched:"
    echo "  $INSTALLED_KWIN_PACKAGE"
    echo "  $SOURCE_KWIN_PACKAGE"
    exit 1
fi

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

echo "==> Installing KDE KeyCast KWin integration..."
echo "KWin package: $KWIN_PACKAGE"

# Disable the plugin while replacing its user-scope package.
"$KWritECONFIG" \
    --file kwinrc \
    --group Plugins \
    --key kde-keycastEnabled \
    --type bool \
    false \
    --notify

# Only remove the user package if its metadata identifies KDE KeyCast.
if [[ -e "$USER_PACKAGE" || -L "$USER_PACKAGE" ]]; then
    if ! is_keycast_package "$USER_PACKAGE"; then
        echo "ERROR: The user package path exists but is not a verified KDE KeyCast package:"
        echo "  $USER_PACKAGE"
        exit 1
    fi

    echo "==> Removing previous user installation..."

    "$KPACKAGETOOL" \
        --type=KWin/Script \
        --remove kde-keycast

    if [[ -e "$USER_PACKAGE" || -L "$USER_PACKAGE" ]]; then
        echo "ERROR: The previous user installation could not be removed."
        exit 1
    fi
fi

echo "==> Installing the current user-scope package..."

"$KPACKAGETOOL" \
    --type=KWin/Script \
    --install "$KWIN_PACKAGE"

if ! is_keycast_package "$USER_PACKAGE"; then
    echo "ERROR: The installed user package could not be verified."
    exit 1
fi

remove_legacy_global_package() {
    [[ -e "$LEGACY_GLOBAL_PACKAGE" || -L "$LEGACY_GLOBAL_PACKAGE" ]] || return 0

    echo "==> Checking legacy global KWin package..."

    if ! is_keycast_package "$LEGACY_GLOBAL_PACKAGE"; then
        echo "ERROR: The legacy path exists but is not a verified KDE KeyCast package:"
        echo "  $LEGACY_GLOBAL_PACKAGE"
        echo "Nothing was removed."
        exit 1
    fi

    if [[ -z "$PKEXEC" ]]; then
        echo "ERROR: pkexec is required to remove the legacy global installation."
        exit 1
    fi

    echo "==> Removing verified legacy global KDE KeyCast installation..."

    "$PKEXEC" /usr/bin/rm -rf -- "$LEGACY_GLOBAL_PACKAGE"

    if [[ -e "$LEGACY_GLOBAL_PACKAGE" || -L "$LEGACY_GLOBAL_PACKAGE" ]]; then
        echo "ERROR: The legacy global installation still exists."
        exit 1
    fi
}

# Remove the legacy copy only after verifying the new user package.
remove_legacy_global_package

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
