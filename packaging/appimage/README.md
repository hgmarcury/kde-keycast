
# KDE KeyCast AppImage

This directory contains the scripts and tools used to build the KDE KeyCast AppImage.

## Contents

The AppImage bundles the components required to run KDE KeyCast independently from a source-tree or development installation, including:

* `kde-keycast`
* the bundled and modified `showmethekey-cli` backend
* required runtime libraries
* Qt platform and Wayland integration components
* KDE KeyCast desktop integration files
* KDE KeyCast icons
* KWin integration files

The bundled `showmethekey-cli` backend is always used. KDE KeyCast does not fall back to an externally installed `showmethekey-cli`.

## Building

The main build script is:

```text
packaging/appimage/build-appimage.sh
```

From the project root, run:

```bash
./packaging/appimage/build-appimage.sh
```

The script expects a successful KDE KeyCast development build in:

```text
build/kde-keycast
```

If necessary, build the project first:

```bash
cmake --build build -j$(nproc)
```

The generated AppImage is placed in:

```text
dist/kde-keycast-x86_64.AppImage
```

### Build tools

The following tools are included in this directory and are used only while building the AppImage:

* `linuxdeploy`
* `linuxdeploy-plugin-qt`
* `appimagetool`

These are build tools and are not runtime dependencies of KDE KeyCast.

## Running the AppImage

Make the AppImage executable:

```bash
chmod +x dist/kde-keycast-x86_64.AppImage
```

Then run it:

```bash
./dist/kde-keycast-x86_64.AppImage
```

KDE KeyCast requires a KDE Plasma Wayland session for its intended input and desktop integration.


## KWin integration

KDE KeyCast's optional KWin integration is installed separately because the KWin script must be registered in the user's KDE configuration.

When using the AppImage, the recommended way to install the integration is:

```bash
./dist/kde-keycast-x86_64.AppImage --install
```

This installs and enables the KDE KeyCast KWin script for the current user, and also creates the user's desktop launcher and icon files.

To remove an AppImage installation, use:

```bash
./dist/kde-keycast-x86_64.AppImage --uninstall
```

This removes the AppImage-installed desktop launcher, icons, and KWin integration. It does not remove KDE KeyCast user configuration.

The bundled `install-kde-keycast` script can also be used to manage the KWin integration directly when working with a CMake installation:

```bash
install-kde-keycast --uninstall
```

The KWin integration is optional. KDE KeyCast itself does not require the KWin script to display keyboard and mouse input.

## Licensing

The KDE KeyCast source code is distributed under:

```text
GPL-3.0-or-later
```

The bundled ShowMeTheKey backend is distributed under:

```text
Apache License 2.0
```

Its original license is preserved in:

```text
vendor/showmethekey-cli/LICENSE
```

The AppImage may also contain third-party runtime libraries and components. Their respective licenses remain applicable to those components.

The build tools used to create the AppImage are separate from the runtime components bundled into it.

For the licensing of third-party components, see the project documentation and `THIRD-PARTY-NOTICES` when provided.

## Notes

The current AppImage build script targets `x86_64` Linux systems and currently uses the Qt and Wayland library paths provided by the build host.

The resulting AppImage is intended for use on Linux systems running KDE Plasma on Wayland.
