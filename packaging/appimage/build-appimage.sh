#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Marc G.
# SPDX-License-Identifier: GPL-3.0-or-later


set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD_DIR="$PROJECT_ROOT/build"
APPDIR="$BUILD_DIR/AppDir"
DIST_DIR="$PROJECT_ROOT/dist"
TOOLS_DIR="$PROJECT_ROOT/packaging/appimage/tools"

LINUXDEPLOY="$TOOLS_DIR/linuxdeploy-x86_64.AppImage"
APPIMAGETOOL="$TOOLS_DIR/appimagetool-x86_64.AppImage"

echo "==> KDE KeyCast AppImage"
echo "    Project: $PROJECT_ROOT"

echo
echo "==> Checking build..."

if [[ ! -x "$BUILD_DIR/kde-keycast" ]]; then
    echo "ERROR: build/kde-keycast not found."
    echo "Run:"
    echo "  cmake --build build -j\$(nproc)"
    exit 1
fi

echo
echo "==> Checking AppImage tools..."

for tool in "$LINUXDEPLOY" "$APPIMAGETOOL"; do
    if [[ ! -x "$tool" ]]; then
        echo "ERROR: required tool not found or not executable:"
        echo "  $tool"
        exit 1
    fi
done

echo
echo "==> Preparing AppDir..."

rm -rf "$APPDIR"
mkdir -p "$APPDIR"
mkdir -p "$DIST_DIR"

echo
echo "==> Installing KDE KeyCast into AppDir..."

cmake --install "$BUILD_DIR" \
    --prefix "$APPDIR/usr"

echo "==> Adding AppImage-specific Polkit policy..."

install -D -m 0644 \
    "$PROJECT_ROOT/resources/polkit/com.github.hgmarcury.kde-keycast-appimage.policy" \
    "$APPDIR/usr/share/polkit-1/actions/com.github.hgmarcury.kde-keycast-appimage.policy"

install \
    -m 0755 \
    "$PROJECT_ROOT/packaging/install-kde-keycast.sh" \
    "$APPDIR/usr/bin/install-kde-keycast"

echo
echo "==> Adding required Qt Wayland components..."

mkdir -p "$APPDIR/usr/plugins/platforms"
mkdir -p "$APPDIR/usr/plugins/wayland-shell-integration"
mkdir -p "$APPDIR/usr/plugins/platformthemes"
mkdir -p "$APPDIR/usr/lib"

cp -v \
    /usr/lib/x86_64-linux-gnu/qt6/plugins/platforms/libqwayland.so \
    "$APPDIR/usr/plugins/platforms/"

cp -v \
    /usr/lib/x86_64-linux-gnu/qt6/plugins/wayland-shell-integration/*.so \
    "$APPDIR/usr/plugins/wayland-shell-integration/"

cp -v \
    /usr/lib/x86_64-linux-gnu/qt6/plugins/platformthemes/KDEPlasmaPlatformTheme6.so \
    "$APPDIR/usr/plugins/platformthemes/"

cp -v \
    /lib/x86_64-linux-gnu/libwayland-client.so.0 \
    "$APPDIR/usr/lib/"

echo
echo "==> Running linuxdeploy..."

QMAKE=/usr/bin/qmake6 \
"$LINUXDEPLOY" \
    --appdir "$APPDIR" \
    --executable "$APPDIR/usr/bin/kde-keycast" \
    --desktop-file "$APPDIR/usr/share/applications/kde-keycast.desktop" \
    --icon-file "$APPDIR/usr/share/icons/hicolor/scalable/apps/keycast-dark.svg" \
    --plugin qt

echo
echo "==> Removing unused Qt image plugins and dependencies..."

rm -rf "$APPDIR/usr/plugins/imageformats"

rm -f \
    "$APPDIR/usr/lib/libaom.so.3" \
    "$APPDIR/usr/lib/libdav1d.so.7" \
    "$APPDIR/usr/lib/libSvtAv1Enc.so.1" \
    "$APPDIR/usr/lib/librav1e.so.0" \
    "$APPDIR/usr/lib/libavif.so.16" \
    "$APPDIR/usr/lib/libjxl.so.0.11" \
    "$APPDIR/usr/lib/libjxl_threads.so.0.11" \
    "$APPDIR/usr/lib/libjxl_cms.so.0.11" \
    "$APPDIR/usr/lib/libOpenEXR-3_1.so.30" \
    "$APPDIR/usr/lib/libheif.so.1" \
    "$APPDIR/usr/lib/libraw.so.23" \
    "$APPDIR/usr/lib/libQt6Pdf.so.6"

echo
echo "==> Creating AppRun launcher..."

rm -f "$APPDIR/AppRun"

cat > "$APPDIR/AppRun" <<'EOF'
#!/usr/bin/env bash

set -e

APPDIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

for plugin_dir in \
    /usr/lib/*/qt6/plugins \
    /usr/lib64/qt6/plugins \
    /usr/lib/qt6/plugins
do
    if [[ -d "$plugin_dir" ]]; then
        export QT_PLUGIN_PATH="$plugin_dir${QT_PLUGIN_PATH:+:$QT_PLUGIN_PATH}"
        break
    fi
done

if [[ "${1:-}" == "--uninstall" ]]; then
    USER_DATA_DIR="${XDG_DATA_HOME:-$HOME/.local/share}"
    KWIN_UNINSTALLER="$APPDIR/usr/bin/uninstall-kwin-kde-keycast"

    echo "==> Uninstalling KDE KeyCast AppImage..."

    if [[ -x "$KWIN_UNINSTALLER" ]]; then
        echo "==> Removing KDE KeyCast KWin integration..."
        if ! "$KWIN_UNINSTALLER"; then
            echo "WARNING: Could not remove the KWin integration."
            echo "Continuing with KDE KeyCast file removal..."
        fi
    fi

    echo "==> Removing KDE KeyCast application integration..."
    rm -f \
        "$USER_DATA_DIR/applications/kde-keycast.desktop" \
        "$USER_DATA_DIR/icons/hicolor/scalable/apps/keycast-light.svg" \
        "$USER_DATA_DIR/icons/hicolor/scalable/apps/keycast-dark.svg"

    # Remove shared permissions only when no native installation remains.
    if command -v kde-keycast >/dev/null 2>&1; then
        echo "==> Native KDE KeyCast detected; preserving shared permissions..."
    else
        PERMISSION_REMOVER="/usr/lib/kde-keycast-appimage/remove-kde-keycast-permissions"

        if [[ ! -x "$PERMISSION_REMOVER" ]]; then
            echo "ERROR: Permission removal helper is missing:"
            echo "  $PERMISSION_REMOVER"
            exit 1
        fi

        echo "==> Removing KDE KeyCast permissions..."
        if ! sudo "$PERMISSION_REMOVER" --apply; then
            echo "ERROR: Could not completely remove KDE KeyCast permissions."
            echo "AppImage system files have been retained for recovery."
            exit 1
        fi
    fi

    echo "==> Removing AppImage-specific backend, permission scripts and Polkit files..."
    if ! sudo rm -f \
        /usr/lib/kde-keycast-appimage/showmethekey-cli \
        /usr/lib/kde-keycast-appimage/configure-kde-keycast-permissions \
        /usr/lib/kde-keycast-appimage/remove-kde-keycast-permissions \
        /usr/lib/kde-keycast-appimage/10-com.github.hgmarcury.kde-keycast.rules \
        /usr/share/polkit-1/actions/com.github.hgmarcury.kde-keycast-appimage.policy; then
        echo "ERROR: Could not remove all AppImage system files."
        exit 1
    fi

    sudo rmdir --ignore-fail-on-non-empty \
        /usr/lib/kde-keycast-appimage

    echo "==> KDE KeyCast AppImage uninstalled."
    exit 0
fi


if [[ "${1:-}" == "--install" ]]; then
    APPIMAGE_PATH="${APPIMAGE:-}"

    if [[ -z "$APPIMAGE_PATH" ]]; then
        echo "ERROR: Cannot determine the AppImage path."
        exit 1
    fi

    APPIMAGE_PATH="$(readlink -f "$APPIMAGE_PATH")"

    exec "$APPDIR/usr/bin/install-kde-keycast" \
        "$APPIMAGE_PATH"
fi

exec "$APPDIR/usr/bin/kde-keycast" "$@"
EOF

chmod +x "$APPDIR/AppRun"

echo
echo "==> Preparing AppStream metadata for appimagetool..."

mv \
    "$APPDIR/kde-keycast.desktop" \
    "$APPDIR/com.github.hgmarcury.kde-keycast.desktop"

cp \
    "$APPDIR/usr/share/metainfo/com.github.hgmarcury.kde-keycast.metainfo.xml" \
    "$APPDIR/usr/share/metainfo/com.github.hgmarcury.kde-keycast.appdata.xml"

rm \
    "$APPDIR/usr/share/metainfo/com.github.hgmarcury.kde-keycast.metainfo.xml"

echo
echo "==> AppDir prepared"

echo
echo "==> AppDir contents:"
find "$APPDIR" -type f -o -type l | sort

echo
echo "==> AppDir size:"
du -sh "$APPDIR"

echo
echo "==> AppDir ready for inspection."
echo "    AppDir: $APPDIR"
echo
echo "==> Generating AppImage..."

APPIMAGE="$DIST_DIR/kde-keycast-x86_64.AppImage"

"$APPIMAGETOOL" \
    --no-appstream \
    "$APPDIR" \
    "$APPIMAGE"

echo
echo "==> AppImage created:"
echo "    $APPIMAGE"
echo
echo "==> AppImage size:"
du -h "$APPIMAGE"
