
cmake_minimum_required(VERSION 3.16)

project(kde-keycast LANGUAGES CXX)

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

    src/kwin/KWinIntegration.h
    src/kwin/KWinIntegration.cpp
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
        ${CMAKE_CURRENT_SOURCE_DIR}/packaging/install-kde-keycast.sh
    DESTINATION
        ${CMAKE_INSTALL_BINDIR}
    RENAME
        install-kde-keycast
)

install(
    PROGRAMS
        ${CMAKE_CURRENT_SOURCE_DIR}/vendor/showmethekey-cli/showmethekey-cli
    DESTINATION
        ${KDE_INSTALL_LIBEXECDIR}/kde-keycast
)

install(
    FILES kde-keycast.desktop
    DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/applications
)

install(
    DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/kwin/kde-keycast/
    DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/kwin/scripts/kde-keycast
)

install(
    FILES ${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/kde-keycast-light.svg
    DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/icons/hicolor/scalable/apps
    RENAME kde-keycast.svg
)
