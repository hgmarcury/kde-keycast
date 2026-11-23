find_package(Qt6 REQUIRED COMPONENTS Widgets)
find_package(KWin REQUIRED)

# Build this KWin effect as a shared plugin without changing the main project.
set(BUILD_SHARED_LIBS ON)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")


kcoreaddons_add_plugin(kde-keycast-cursor
    SOURCES
        cursortracking.cpp
    INSTALL_NAMESPACE
        "kwin/effects/plugins"
)

set_property(
    TARGET kde-keycast-cursor
    APPEND PROPERTY AUTOMOC_MACRO_NAMES
    KWIN_EFFECT_FACTORY
)

target_compile_features(kde-keycast-cursor PRIVATE cxx_std_20)

if(KWin_VERSION VERSION_LESS "6.8")
    target_compile_definitions(kde-keycast-cursor PRIVATE KDE_KEYCAST_KWIN_LEGACY_API=1)
    message(STATUS "KWin ${KWin_VERSION}: using legacy cursor effect API")
else()
    message(STATUS "KWin ${KWin_VERSION}: using current cursor effect API")
endif()

target_link_libraries(kde-keycast-cursor
    PRIVATE
        KWin::kwin
)
