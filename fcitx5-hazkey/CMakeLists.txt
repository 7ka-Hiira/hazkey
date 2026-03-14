cmake_minimum_required(VERSION 3.21)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
    message(FATAL_ERROR
        "In-source builds are not allowed. "
        "Please create a separate build directory and run cmake from there.\n"
        "To clean up, remove CMakeCache.txt and CMakeFiles/ directory.")
endif()

project(fcitx5-hazkey VERSION 0.2.1)
include(GNUInstallDirs)

set(CMAKE_CXX_STANDARD 20)

add_definitions(-DFCITX_GETTEXT_DOMAIN=\"fcitx5-hazkey\")

find_package(Protobuf REQUIRED)

find_package(Fcitx5Core REQUIRED)
find_package(Fcitx5Utils REQUIRED)
find_package(Fcitx5Config REQUIRED)

include("${FCITX_INSTALL_CMAKECONFIG_DIR}/Fcitx5Utils/Fcitx5CompilerSettings.cmake")

fcitx5_add_i18n_definition()

find_package(Gettext REQUIRED)

if(HAZKEY_FLATPAK)
    set(HAZKEY_ICON_NAME "org.fcitx.Fcitx5.Addon.Hazkey")
    # Flatpak sandbox の PATH に拡張の bin/ が含まれないためフルパスで指定。
    # このパスはマニフェストの prefix (/app/addons/Hazkey) と一致させる必要がある。
    set(HAZKEY_SETTINGS_COMMAND "/app/addons/Hazkey/bin/hazkey-settings")
else()
    set(HAZKEY_ICON_NAME "hazkey")
    set(HAZKEY_SETTINGS_COMMAND "hazkey-settings")
endif()

add_subdirectory(po)
add_subdirectory(src)

fcitx5_translate_desktop_file(org.fcitx.Fcitx5.Addon.Hazkey.metainfo.xml.in
                              org.fcitx.Fcitx5.Addon.Hazkey.metainfo.xml XML)

install(FILES "${CMAKE_CURRENT_BINARY_DIR}/org.fcitx.Fcitx5.Addon.Hazkey.metainfo.xml" DESTINATION "${CMAKE_INSTALL_FULL_DATADIR}/metainfo")
