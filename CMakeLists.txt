# SPDX-FileCopyrightText: 2026 Marc G.
# SPDX-License-Identifier: GPL-3.0-or-later


cmake_minimum_required(VERSION 3.16)

project(kde-keycast
  VERSION 1.0.0
  LANGUAGES C CXX
)

find_package(ECM REQUIRED NO_MODULE)
set(CMAKE_MODULE_PATH ${ECM_MODULE_PATH})

include(KDEInstallDirs)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS
    Widgets
    DBus
    Core
)

find_package(PkgConfig REQUIRED)

find_package(Threads REQUIRED)

find_path(LIBINPUT_INCLUDE_DIR
    NAMES libinput.h
    REQUIRED
)

find_path(LIBUDEV_INCLUDE_DIR
    NAMES libudev.h
    REQUIRED
)

find_path(LIBEVDEV_INCLUDE_DIR
    NAMES libevdev/libevdev.h
    PATH_SUFFIXES libevdev-1.0
    REQUIRED
)

find_library(LIBINPUT_LIBRARY
    NAMES input
    REQUIRED
)

find_library(LIBUDEV_LIBRARY
    NAMES udev
    REQUIRED
)

find_library(LIBEVDEV_LIBRARY
    NAMES evdev
    REQUIRED
)

pkg_check_modules(XKBCOMMON REQUIRED
    xkbcommon
)

pkg_check_modules(WAYLAND REQUIRED
    wayland-client
)

add_executable(kde-keycast
    main.cpp
    resources.qrc

    src/core/VisualEntry.h
    src/core/Shortcuts.h

    src/input/InputEvent.h
    src/input/InputBackend.h
    src/input/ShowMeTheKeyBackend.h
    src/input/ShowMeTheKeyBackend.cpp
    src/input/InputReader.h
    src/input/InputReader.cpp

    src/settings/Settings.h
    src/settings/Settings.cpp

    src/xkb/XkbKeyboard.h
    src/xkb/XkbManager.h
    src/xkb/XkbManager.cpp

    src/mouse/MouseInput.h
    src/mouse/WaylandPointer.h
    src/mouse/WaylandPointer.cpp

    src/wayland/WaylandConnection.h
    src/wayland/WaylandConnection.cpp
    src/wayland/WaylandKeyboard.h
    src/wayland/WaylandKeyboard.cpp

    src/ui/ShortcutHelpDialog.h
    src/ui/ShortcutHelpDialog.cpp
    src/ui/AboutDialog.h
    src/ui/AboutDialog.cpp
    src/ui/ContextMenu.h
    src/ui/ContextMenu.cpp
    src/ui/StatusToast.cpp

    src/kwin/KWinIntegration.h
    src/kwin/KWinIntegration.cpp
    src/kwin/KWinIntegrationDialog.h
    src/kwin/KWinIntegrationDialog.cpp
)

add_executable(showmethekey-cli
    vendor/showmethekey-cli/main.c
)


target_include_directories(showmethekey-cli PRIVATE
    ${LIBINPUT_INCLUDE_DIR}
    ${LIBUDEV_INCLUDE_DIR}
    ${LIBEVDEV_INCLUDE_DIR}
)

target_link_libraries(showmethekey-cli PRIVATE
    ${LIBINPUT_LIBRARY}
    ${LIBUDEV_LIBRARY}
    ${LIBEVDEV_LIBRARY}
    Threads::Threads
)

configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/metainfo/com.github.hgmarcury.kde-keycast.metainfo.xml.in
    ${CMAKE_CURRENT_BINARY_DIR}/com.github.hgmarcury.kde-keycast.metainfo.xml
    @ONLY
)

configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/packaging/uninstall-kde-keycast.sh.in
    ${CMAKE_CURRENT_BINARY_DIR}/uninstall-kde-keycast
    @ONLY
)

target_compile_definitions(kde-keycast PRIVATE
    KDE_KEYCAST_LIBEXECDIR="${KDE_INSTALL_LIBEXECDIR}"
)

target_include_directories(kde-keycast PRIVATE
    ${XKBCOMMON_INCLUDE_DIRS}
    ${WAYLAND_INCLUDE_DIRS}
)

target_link_directories(kde-keycast PRIVATE
    ${XKBCOMMON_LIBRARY_DIRS}
    ${WAYLAND_LIBRARY_DIRS}
)

target_compile_options(kde-keycast PRIVATE
    ${XKBCOMMON_CFLAGS_OTHER}
    ${WAYLAND_CFLAGS_OTHER}
)

target_link_libraries(kde-keycast PRIVATE
    Qt6::Widgets
    Qt6::DBus
    Qt6::Core
    ${XKBCOMMON_LIBRARIES}
    ${WAYLAND_LIBRARIES}
)

install(
    TARGETS kde-keycast
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(
    PROGRAMS
        ${CMAKE_CURRENT_SOURCE_DIR}/packaging/install-kwin-kde-keycast.sh
    DESTINATION
        ${CMAKE_INSTALL_BINDIR}
    RENAME
        install-kwin-kde-keycast
)

install(
    PROGRAMS
        ${CMAKE_CURRENT_SOURCE_DIR}/packaging/uninstall-kwin-kde-keycast.sh
    DESTINATION
        ${CMAKE_INSTALL_BINDIR}
    RENAME
        uninstall-kwin-kde-keycast
)

install(
    DIRECTORY
        ${CMAKE_CURRENT_SOURCE_DIR}/kwin/kde-keycast
    DESTINATION
        ${CMAKE_INSTALL_DATAROOTDIR}/kde-keycast/kwin
)

install(
    PROGRAMS
        ${CMAKE_CURRENT_BINARY_DIR}/uninstall-kde-keycast
    DESTINATION
        ${CMAKE_INSTALL_BINDIR}
)

install(
    TARGETS showmethekey-cli
    RUNTIME DESTINATION ${KDE_INSTALL_LIBEXECDIR}/kde-keycast
)

install(
    FILES kde-keycast.desktop
    DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/applications
)

install(
    FILES
        ${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/keycast-light.svg
        ${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/keycast-dark.svg
    DESTINATION
        ${CMAKE_INSTALL_DATAROOTDIR}/icons/hicolor/scalable/apps
)

install(
    FILES
        ${CMAKE_CURRENT_BINARY_DIR}/com.github.hgmarcury.kde-keycast.metainfo.xml
    DESTINATION
        ${CMAKE_INSTALL_DATAROOTDIR}/metainfo
)
