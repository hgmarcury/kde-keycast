cmake_minimum_required(VERSION 3.16)

project(kde-keycast LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS
    Widgets
    DBus
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
    Qt6::DBus
    ${XKBCOMMON_LIBRARIES}
    ${WAYLAND_LIBRARIES}
)
