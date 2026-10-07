
<p align="center"> <picture> <source media="(prefers-color-scheme: dark)" srcset="resources/icons/keycast-light.svg"> <source media="(prefers-color-scheme: light)" srcset="resources/icons/keycast-dark.svg"> <img src="resources/icons/keycast-dark.svg" width="180" alt="KDE KeyCast"> </picture> </p>

<h1 align="center">KDE KeyCast</h1>

<p align="center"> Visual keyboard and mouse indicator for Linux/KDE, designed especially for presentations, tutorials, and screencasts. </p>

<p align="center">
  <img src="resources/screenshots/kde-keycast-demo.gif" alt="KDE KeyCast demonstration" width="800">
</p>

<p align="center">
  <img src="resources/screenshots/kde-keycast.png" alt="KDE KeyCast screenshot" width="800">
</p>

KDE KeyCast displays pressed keys and key combinations visually, using XKB to correctly interpret keyboard input according to the active keyboard layout.

## Features

* Captures keyboard events using the bundled ShowMeTheKey backend.
* Uses XKB to correctly resolve keys according to the system keyboard layout.
* Displays characters produced by `Shift`.
* Displays combinations involving `Ctrl`, `Alt`, `AltGr`, `Meta`, and `Shift` when appropriate.
* Represents special keys using appropriate names or symbols.
* Detects and displays mouse events, including mouse wheel scrolling.
* Includes a compact mode for displaying only relevant key combinations.
* Provides KDE/Plasma integration through a system tray icon.
* Includes optional KWin integration.

## Project status

KDE KeyCast is currently under development.

The main codebase has been reorganized into independent components to make the project easier to maintain and extend.

KDE KeyCast currently provides CMake-based installation and an AppImage build, including optional KWin integration.

## Dependencies

### Build dependencies

KDE KeyCast requires the following development dependencies:

* CMake
* A C/C++ compiler with C++17 support
* Extra CMake Modules (ECM)
* pkg-config
* Qt 6
* libxkbcommon
* Wayland
* libinput
* libudev
* libevdev

The project does not require an external installation of `showmethekey-cli`.

The ShowMeTheKey backend used by KDE KeyCast is included in the project and compiled from source. It is a modified version of ShowMeTheKey that adds support for detecting physical mouse wheel events.

### Runtime dependencies

Once installed, KDE KeyCast uses the corresponding runtime libraries for:

* Qt 6
* libxkbcommon
* Wayland
* libinput
* libudev
* libevdev

These libraries are normally provided by a modern Linux distribution running KDE/Plasma.

### Optional KWin integration

KWin integration requires the following KDE tools:

* `kpackagetool6`
* `kwriteconfig6`
* `qdbus6`

These tools are not required to run KDE KeyCast normally.

## Building

The project uses CMake.

For example:

```bash
cmake -S . -B build
cmake --build build -j$(nproc)
```

To install into a local test directory:

```bash
cmake --install build --prefix "$(pwd)/build/test-install"
```

The following package instructions apply to building KDE KeyCast from source.

## Installing from CMake

After building the project, KDE KeyCast can be installed into a chosen prefix:

```bash
cmake --install build --prefix "$HOME/.local"
```

For a system-wide installation, use a suitable system prefix with the required privileges:

```bash
sudo cmake --install build --prefix /usr/local
```

The installation includes KDE KeyCast, the bundled ShowMeTheKey backend, the desktop entry, icons, metadata, and the KWin integration files.

It also installs the following helper scripts:

* `install-kde-keycast`
* `uninstall-kde-keycast`

KWin integration is optional and is not enabled automatically by the CMake installation.

## Uninstalling

A CMake installation can be removed using the installed `uninstall-kde-keycast` script.

For example, when installed into `/usr/local`:

```bash
sudo /usr/local/bin/uninstall-kde-keycast
```

When installed into the user's local prefix:

```bash
~/.local/bin/uninstall-kde-keycast
```

The uninstall script removes the installed KDE KeyCast files and KWin integration. User configuration is preserved.

### Debian / Ubuntu

```bash
sudo apt install \
    build-essential \
    cmake \
    extra-cmake-modules \
    pkg-config \
    qt6-base-dev \
    libxkbcommon-dev \
    libwayland-dev \
    libinput-dev \
    libudev-dev \
    libevdev-dev
```

### Fedora

```bash
sudo dnf install \
    gcc \
    gcc-c++ \
    cmake \
    extra-cmake-modules \
    pkgconf-pkg-config \
    qt6-qtbase-devel \
    libxkbcommon-devel \
    wayland-devel \
    libinput-devel \
    systemd-devel \
    libevdev-devel
```

### Arch Linux

```bash
sudo pacman -S \
    base-devel \
    cmake \
    extra-cmake-modules \
    pkgconf \
    qt6-base \
    libxkbcommon \
    wayland \
    libinput \
    libevdev
```

Package names may vary between distribution releases and derivatives.

## Optional KWin integration

KDE KeyCast includes a script for optionally installing its KWin integration:

```text
install-kde-keycast
```

KWin integration is not enabled automatically when KDE KeyCast is installed.

The script can install, update, or remove the KWin script and reconfigure KWin when necessary.

KDE KeyCast can run normally without KWin integration.

## AppImage

The project includes tools and scripts for building an AppImage.

The resulting AppImage can be run directly without installation:

```bash
./dist/kde-keycast-x86_64.AppImage
```

The AppImage also provides optional installation and uninstallation helpers.

To install the AppImage for the current user:

```bash
./dist/kde-keycast-x86_64.AppImage --install
```

This creates the user's desktop launcher and icon files and installs and enables the optional KDE KeyCast KWin integration.

To remove the AppImage installation:

```bash
./dist/kde-keycast-x86_64.AppImage --uninstall
```

This removes the AppImage-installed launcher, icons, and KWin integration. User configuration is preserved.

Detailed instructions for building the AppImage are available in:

```text
packaging/appimage/README.md
```

## License

The KDE KeyCast code is distributed under:

**GNU General Public License v3.0 or later (GPL-3.0-or-later)**

The full license text is available in:

```text
LICENSE
```

The bundled ShowMeTheKey backend is a separate component distributed under:

**Apache License 2.0**

Its original license is preserved in:

```text
vendor/showmethekey-cli/LICENSE
```

The backend has been modified for KDE KeyCast, including support for physical mouse wheel events.

Third-party libraries and components retain their respective licenses.

## Copyright

Copyright © 2026 Marc G.

KDE KeyCast is not an official KDE project and is not affiliated with KDE e.V.

## Contributing

Contributions, fixes, and suggestions are welcome.

For significant changes, opening a discussion or issue before implementation is recommended.

## Project

KDE KeyCast is developed as an independent project for Linux/KDE.

The goal is to provide a simple input indicator suitable for presentations, recordings, and screencasts.
