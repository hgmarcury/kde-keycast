#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Marc G.
# SPDX-License-Identifier: GPL-3.0-or-later

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
APPDIR="$(cd "$SCRIPT_DIR/../.." && pwd)"

USER_DATA_DIR="${XDG_DATA_HOME:-$HOME/.local/share}"
APPLICATIONS_DIR="$USER_DATA_DIR/applications"
ICONS_DIR="$USER_DATA_DIR/icons/hicolor/scalable/apps"

APPIMAGE_PATH="${1:-}"

if [[ -z "$APPIMAGE_PATH" ]]; then
    echo "ERROR: AppImage path not specified."
    exit 1
fi

APPIMAGE_PATH="$(readlink -f "$APPIMAGE_PATH")"

if [[ ! -f "$APPIMAGE_PATH" ]]; then
    echo "ERROR: AppImage not found:"
    echo "  $APPIMAGE_PATH"
    exit 1
fi

DESKTOP_TEMPLATE="$APPDIR/usr/share/applications/kde-keycast.desktop"

if [[ ! -f "$DESKTOP_TEMPLATE" ]]; then
    echo "ERROR: Desktop file not found:"
    echo "  $DESKTOP_TEMPLATE"
    exit 1
fi

LIGHT_ICON="$APPDIR/usr/share/icons/hicolor/scalable/apps/keycast-light.svg"
DARK_ICON="$APPDIR/usr/share/icons/hicolor/scalable/apps/keycast-dark.svg"

if [[ ! -f "$LIGHT_ICON" || ! -f "$DARK_ICON" ]]; then
    echo "ERROR: KDE KeyCast icons not found."
    exit 1
fi

echo "==> Installing KDE KeyCast application integration..."

mkdir -p \
    "$APPLICATIONS_DIR" \
    "$ICONS_DIR"

sed \
    "s|^Exec=.*|Exec=$APPIMAGE_PATH|" \
    "$DESKTOP_TEMPLATE" \
    > "$APPLICATIONS_DIR/kde-keycast.desktop"

chmod 0644 \
    "$APPLICATIONS_DIR/kde-keycast.desktop"

install \
    -m 0644 \
    "$LIGHT_ICON" \
    "$ICONS_DIR/keycast-light.svg"

install \
    -m 0644 \
    "$DARK_ICON" \
    "$ICONS_DIR/keycast-dark.svg"

echo "==> KDE KeyCast application integration installed."
