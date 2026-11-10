#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Marc G.
# SPDX-License-Identifier: GPL-3.0-or-later

set -euo pipefail

REMOVE_SHARED_PACKAGE=false

case "${1:-}" in
"") ;;
--remove-shared-package)
    REMOVE_SHARED_PACKAGE=true
    ;;
*)
    echo "ERROR: Unknown argument: $1"
    exit 1
    ;;
esac

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
SHARED_GLOBAL_PACKAGE="/usr/share/kde-keycast/kwin/kde-keycast"

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

# Remove known global installations only after verifying their identity.
remove_global_package() {
    local package_path="$1"
    local description="$2"

    if [[ ! -e "$package_path" && ! -L "$package_path" ]]; then
        return 0
    fi

    if ! is_keycast_package "$package_path"; then
        echo "ERROR: $description is not a verified KDE KeyCast package:"
        echo "  $package_path"
        return 1
    fi

    if [[ -z "$PKEXEC" ]]; then
        echo "ERROR: pkexec is required to remove the global installation."
        return 1
    fi

    echo "==> Removing verified $description..."

    "$PKEXEC" /usr/bin/rm -rf -- "$package_path"

    if [[ -e "$package_path" || -L "$package_path" ]]; then
        echo "ERROR: The global package still exists:"
        echo "  $package_path"
        return 1
    fi
}

# Remove the shared package only during a complete application uninstall.
if [[ "$REMOVE_SHARED_PACKAGE" == true ]]; then
    remove_global_package \
        "$SHARED_GLOBAL_PACKAGE" \
        "shared global KDE KeyCast package"
fi

# The legacy global package is no longer used by KDE KeyCast.
remove_global_package \
    "$LEGACY_GLOBAL_PACKAGE" \
    "legacy global KDE KeyCast package"

if [[ "$REMOVE_SHARED_PACKAGE" == true ]]; then
    # Remove only existing, empty shared data directories.
    SHARED_DIRECTORIES=()

    for directory in \
        /usr/share/kde-keycast/kwin \
        /usr/share/kde-keycast/polkit \
        /usr/share/kde-keycast; do
        if [[ -d "$directory" ]]; then
            SHARED_DIRECTORIES+=("$directory")
        fi
    done

    if [[ ${#SHARED_DIRECTORIES[@]} -gt 0 ]]; then
        if [[ -z "$PKEXEC" ]]; then
            echo "ERROR: pkexec is required to clean the shared data directories."
            exit 1
        fi

        "$PKEXEC" /usr/bin/rmdir --ignore-fail-on-non-empty -- \
            "${SHARED_DIRECTORIES[@]}"
    fi

    if [[ ! -e "$SHARED_GLOBAL_PACKAGE" &&
        ! -L "$SHARED_GLOBAL_PACKAGE" &&
        ! -e "$LEGACY_GLOBAL_PACKAGE" &&
        ! -L "$LEGACY_GLOBAL_PACKAGE" ]]; then
        echo "==> No remaining global KDE KeyCast packages."
    fi
else
    echo "==> Shared global KDE KeyCast package retained for future installation."
fi
echo "==> Reconfiguring KWin..."

"$QDBUS" \
    org.kde.KWin \
    /KWin \
    org.kde.KWin.reconfigure

echo "==> KDE KeyCast KWin integration uninstalled."
