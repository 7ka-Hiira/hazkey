cmake_minimum_required(VERSION 3.21)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
    message(FATAL_ERROR
        "In-source builds are not allowed. "
        "Please create a separate build directory and run cmake from there.\n"
        "To clean up, remove CMakeCache.txt and CMakeFiles/ directory.")
endif()

project(hazkey-server VERSION 0.2.1)
include(GNUInstallDirs)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
# Skip ALL targets during install to avoid swift permission issues
set(CMAKE_SKIP_INSTALL_ALL_DEPENDENCY TRUE)

find_program(SWIFT_EXECUTABLE swift REQUIRED)

option(HAZKEY_SERVER_STATIC_SWIFT_STDLIB "Statically link swift standard libraries" ON)
set(HAZKEY_SERVER_SWIFT_SDK "" CACHE STRING "Swift SDK identifier/path used for cross compile")
set(HAZKEY_SERVER_SWIFT_LTO_MODE "" CACHE STRING "Swift LTO Mode")

option(HAZKEY_SERVER_ENABLE_ZENZAI "Enable Zenzai AI features" ON)
option(HAZKEY_SERVER_BUILD_LLAMA_LIBS "Build llama.cpp libraries" ${HAZKEY_SERVER_ENABLE_ZENZAI})
option(HAZKEY_SERVER_INSTALL_LLAMA_LIBS "Install llama.cpp libraries" ${HAZKEY_SERVER_BUILD_LLAMA_LIBS})
option(HAZKEY_SERVER_ZENZAI_TRAIT "Enable Zenzai trait in AzooKeyKanaKanjiConverter" ${HAZKEY_SERVER_ENABLE_ZENZAI})

set(HAZKEY_SERVER_SYSTEM_RESOURCE_PATH "${CMAKE_INSTALL_FULL_DATADIR}" CACHE PATH "System Resource path")
set(HAZKEY_SERVER_SYSTEM_LIBRARY_PATH "${CMAKE_INSTALL_FULL_LIBDIR}" CACHE PATH "System Resource path")
option(HAZKEY_SERVER_INSTALL_DICTIONARY "Install dictionary" ON)

# TODO: check path reliability
set(HAZKEY_SERVER_LIBLLAMA_BINDIR "${CMAKE_BINARY_DIR}/bin" CACHE PATH "Llama.cpp library directory" )

# --- hazkey-server --- #

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(_DEFAULT_SWIFT_TYPE "debug")
else()
    set(_DEFAULT_SWIFT_TYPE "release")
endif()
set(SWIFT_BUILD_TYPE "${_DEFAULT_SWIFT_TYPE}" CACHE STRING "Swift build type (debug/release)")

message(STATUS "Configuring constants.swift")
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/Sources/hazkey-server/constants.swift.in
    ${CMAKE_CURRENT_SOURCE_DIR}/Sources/hazkey-server/constants.swift
    @ONLY
)

# Auto detect swift library path
# To bypass issue swiftlang/swift#78003
if(HAZKEY_SERVER_STATIC_SWIFT_STDLIB AND NOT SWIFT_DYNAMIC_LIB_PATH)
    if(SWIFT_EXECUTABLE)

        execute_process(
            COMMAND ${SWIFT_EXECUTABLE} -print-target-info
            OUTPUT_VARIABLE SWIFT_TARGET_INFO_JSON
            RESULT_VARIABLE SWIFT_INFO_RESULT
        )
        if(NOT SWIFT_INFO_RESULT EQUAL 0)
            message(WARNING "Failed to get Swift target info. Build may fail due to issue swiftlang/swift#78003.")
        endif()

        string(JSON SWIFT_DYNAMIC_LIB_PATH GET ${SWIFT_TARGET_INFO_JSON} "paths" "runtimeLibraryPaths" 0)
        message(STATUS "Detected Swift dynamic library path: ${SWIFT_DYNAMIC_LIB_PATH}")
    else()
        message(WARNING "Swift executable not found in PATH.")
    endif()
endif()

add_custom_target(build_hazkey_server ALL
    COMMAND ${CMAKE_COMMAND}
        "-DSWIFT_BUILD_TYPE=${SWIFT_BUILD_TYPE}"
        "-DSWIFT_EXECUTABLE=${SWIFT_EXECUTABLE}"
        "-DSWIFT_STATIC_STDLIB=${HAZKEY_SERVER_STATIC_SWIFT_STDLIB}"
        "-DSWIFT_DISABLE_DEPENDENCY_CACHE=${SWIFT_DISABLE_DEPENDENCY_CACHE}"
        "-DSWIFT_DYNAMIC_LIB_PATH=${SWIFT_DYNAMIC_LIB_PATH}"
        "-DSWIFT_LINK_PATH=${SWIFT_LINK_PATH}"
        "-DSWIFT_WORK_DIR=${CMAKE_CURRENT_SOURCE_DIR}"
        "-DHAZKEY_SERVER_SWIFT_SDK=${HAZKEY_SERVER_SWIFT_SDK}"
        "-DHAZKEY_SERVER_SWIFT_LTO_MODE=${HAZKEY_SERVER_SWIFT_LTO_MODE}"
        "-DHAZKEY_SERVER_ZENZAI_TRAIT=${HAZKEY_SERVER_ZENZAI_TRAIT}"
        "-DLIBLLAMA_DIR=${HAZKEY_SERVER_LIBLLAMA_BINDIR}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/build_swift.cmake"
    WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    COMMENT "Building hazkey-server"
    VERBATIM
)


# ----- llama.cpp ----- #
if(HAZKEY_SERVER_BUILD_LLAMA_LIBS)
    if(CMAKE_BUILD_TYPE STREQUAL "Release")
        set(GGML_LTO ON CACHE BOOL "")
    endif()

    if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(arm|aarch64)")
        set(GGML_CPU_ALL_VARIANTS ON CACHE BOOL "")
        set(GGML_CPU_ARM_ARCH "armv8-a" CACHE STRING "")
    else()
        set(GGML_CPU_ALL_VARIANTS ON CACHE BOOL "")
    endif()

    set(CMAKE_INSTALL_RPATH "$ORIGIN")
    set(CMAKE_BUILD_WITH_INSTALL_RPATH ON)
    set(CMAKE_SKIP_BUILD_RPATH OFF)

    set(BUILD_SHARED_LIBS ON CACHE BOOL "")
    set(LLAMA_CURL OFF CACHE BOOL "")
    set(LLAMA_STANDALONE OFF CACHE BOOL "")
    set(GGML_NATIVE OFF CACHE BOOL "")
    set(GGML_BACKEND_DL ON CACHE BOOL "")
    set(GGML_CPU ON CACHE BOOL "")
    set(GGML_VULKAN ON CACHE BOOL "")
    set(GGML_CUDA OFF CACHE BOOL "")
    set(GGML_HIP OFF CACHE BOOL "")
    set(CMAKE_SKIP_BUILD_RPATH ON CACHE BOOL "")
    add_subdirectory(llama.cpp EXCLUDE_FROM_ALL)

    add_dependencies(build_hazkey_server llama)
endif()

# ---- install ---- #

# install hazkey-server
install(PROGRAMS ${CMAKE_CURRENT_BINARY_DIR}/swift-build/${SWIFT_BUILD_TYPE}/hazkey-server
        DESTINATION ${CMAKE_INSTALL_FULL_LIBDIR}/hazkey)

# configure and install hazkey-server wrapper script
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/hazkey-server.sh.in
    ${CMAKE_CURRENT_BINARY_DIR}/hazkey-server
    @ONLY
)

install(PROGRAMS ${CMAKE_CURRENT_BINARY_DIR}/hazkey-server
        DESTINATION ${CMAKE_INSTALL_FULL_BINDIR})

# install llama libraries
if(HAZKEY_SERVER_INSTALL_LLAMA_LIBS)
    install(FILES
        "${HAZKEY_SERVER_LIBLLAMA_BINDIR}/libggml-base.so"
        "${HAZKEY_SERVER_LIBLLAMA_BINDIR}/libggml.so"
        "${HAZKEY_SERVER_LIBLLAMA_BINDIR}/libllama.so"
        DESTINATION "${CMAKE_INSTALL_FULL_LIBDIR}/hazkey/libllama"
    )

    install(DIRECTORY "${HAZKEY_SERVER_LIBLLAMA_BINDIR}/"
        DESTINATION "${CMAKE_INSTALL_FULL_LIBDIR}/hazkey/libllama/backends"
        FILES_MATCHING
        PATTERN "*.so"
        PATTERN "libggml-base.so" EXCLUDE
        PATTERN "libggml.so" EXCLUDE
        PATTERN "libllama.so" EXCLUDE
    )
endif()

# install icons
if(HAZKEY_FLATPAK)
    set(HAZKEY_ICON_NAME "org.hazkey.Fcitx5.Addon.Hazkey")
else()
    set(HAZKEY_ICON_NAME "hazkey")
endif()
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/icons/hazkey.svg
        DESTINATION ${CMAKE_INSTALL_FULL_DATADIR}/icons/hicolor/scalable/apps)
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/icons/hazkey_256x256.png
        RENAME ${HAZKEY_ICON_NAME}.png
        DESTINATION ${CMAKE_INSTALL_FULL_DATADIR}/icons/hicolor/256x256/apps)
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/icons/hazkey_48x48.png
        RENAME ${HAZKEY_ICON_NAME}.png
        DESTINATION ${CMAKE_INSTALL_FULL_DATADIR}/icons/hicolor/48x48/apps)
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/icons/hazkey_32x32.png
        RENAME ${HAZKEY_ICON_NAME}.png
        DESTINATION ${CMAKE_INSTALL_FULL_DATADIR}/icons/hicolor/32x32/apps)
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/icons/hazkey_24x24.png
        RENAME ${HAZKEY_ICON_NAME}.png
        DESTINATION ${CMAKE_INSTALL_FULL_DATADIR}/icons/hicolor/24x24/apps)

# install dictionary
if(HAZKEY_SERVER_INSTALL_DICTIONARY)

    set(_OLD_INSTALL_MESSAGE ${CMAKE_INSTALL_MESSAGE})
    if(NOT CMAKE_INSTALL_MESSAGE)
        set(CMAKE_INSTALL_MESSAGE "LAZY")
    endif()

    install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/azooKey_dictionary_storage/Dictionary
        DESTINATION ${CMAKE_INSTALL_FULL_DATADIR}/hazkey)

    set(CMAKE_INSTALL_MESSAGE ${_OLD_INSTALL_MESSAGE})
endif()

#install emoji dictionary
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/azooKey_emoji_dictionary_storage/EmojiDictionary/emoji_all_E16.0.txt
        DESTINATION ${CMAKE_INSTALL_FULL_DATADIR}/hazkey)
