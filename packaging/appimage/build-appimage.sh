#!/usr/bin/env bash

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
    --icon-file "$APPDIR/usr/share/icons/hicolor/scalable/apps/kde-keycast.svg" \
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

exec "$APPDIR/usr/bin/kde-keycast" "$@"
EOF

chmod +x "$APPDIR/AppRun"

echo
echo "==> AppDir prepared by linuxdeploy."

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
echo "    AppImage generation is intentionally not performed yet."
echo "    We will inspect bundled libraries first."
