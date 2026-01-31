cmake_minimum_required(VERSION 3.21)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
    message(FATAL_ERROR
        "In-source builds are not allowed. "
        "Please create a separate build directory and run cmake from there.\n"
        "To clean up, remove CMakeCache.txt and CMakeFiles/ directory.")
endif()

project(hazkey-settings VERSION 0.2.0 LANGUAGES CXX)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets LinguistTools Network)
find_package(Protobuf REQUIRED)

set(TS_FILES hazkey-settings_ja_JP.ts)

# Proto files
set(PROTO_FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/../protocol/base.proto
    ${CMAKE_CURRENT_SOURCE_DIR}/../protocol/commands.proto
    ${CMAKE_CURRENT_SOURCE_DIR}/../protocol/config.proto
)

set(PROJECT_SOURCES
    main.cpp
    mainwindow.cpp
    mainwindow.h
    mainwindow.ui
    serverconnector.cpp
    serverconnector.h
    config_definitions.h
    config_macros.h
    ${TS_FILES}
)

qt_add_executable(hazkey-settings ${PROJECT_SOURCES}
    hazkey-icon.qrc)
qt_create_translation(QM_FILES ${PROJECT_SOURCES} ${TS_FILES})

foreach(_qm ${QM_FILES})
    get_filename_component(_qm_name ${_qm} NAME)
    set_source_files_properties(${_qm} PROPERTIES QT_RESOURCE_ALIAS ${_qm_name})
endforeach()

qt_add_resources(hazkey-settings "translations"
    PREFIX "/i18n"
    FILES ${QM_FILES}
)

if(Protobuf_VERSION VERSION_GREATER_EQUAL "3.15")
    # 3.15 ~：stable proto3 optional support
    message(STATUS "Using standard protobuf_generate (protobuf ${Protobuf_VERSION})")
    protobuf_generate(
        TARGET hazkey-settings
        LANGUAGE cpp
        PROTOS ${PROTO_FILES}
        IMPORT_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/../protocol
        PROTOC_OUT_DIR ${CMAKE_CURRENT_BINARY_DIR}
    )
elseif(Protobuf_VERSION VERSION_GREATER_EQUAL "3.12")
    # 3.12 ~ 3.14: build with flag. cannot use PROTOC_OPTIONS.
    # Ubuntu 22.04: v3.12.4 (2025/8/28)
    message(STATUS "Using manual protoc execution for proto3 optional (protobuf ${Protobuf_VERSION})")

    set(PROTO_SRCS)
    set(PROTO_HDRS)

    foreach(PROTO_FILE ${PROTO_FILES})
        get_filename_component(PROTO_NAME ${PROTO_FILE} NAME_WE)
        get_filename_component(PROTO_PATH ${PROTO_FILE} ABSOLUTE)
        set(PROTO_SRC "${CMAKE_CURRENT_BINARY_DIR}/${PROTO_NAME}.pb.cc")
        set(PROTO_HDR "${CMAKE_CURRENT_BINARY_DIR}/${PROTO_NAME}.pb.h")

        list(APPEND PROTO_SRCS ${PROTO_SRC})
        list(APPEND PROTO_HDRS ${PROTO_HDR})

        add_custom_command(
            OUTPUT ${PROTO_SRC} ${PROTO_HDR}
            COMMAND ${Protobuf_PROTOC_EXECUTABLE}
            ARGS --cpp_out=${CMAKE_CURRENT_BINARY_DIR}
                 --experimental_allow_proto3_optional
                 --proto_path=${CMAKE_CURRENT_SOURCE_DIR}/../protocol
                 ${CMAKE_CURRENT_SOURCE_DIR}/../protocol/${PROTO_NAME}.proto
            DEPENDS ${PROTO_FILE}
            COMMENT "Generating C++ protobuf files from ${PROTO_FILE}"
            VERBATIM
        )
    endforeach()

    target_sources(hazkey-settings PRIVATE ${PROTO_SRCS} ${PROTO_HDRS})
else()
    # ~ 3.12：no proto3 optional support
    message(FATAL_ERROR "protobuf 3.12+ required for proto3 optional support. Current version: ${Protobuf_VERSION}")
endif()

target_include_directories(hazkey-settings PRIVATE
    ${CMAKE_CURRENT_BINARY_DIR}
    ${Protobuf_INCLUDE_DIRS}
)

target_link_libraries(hazkey-settings PRIVATE
    Qt6::Widgets
    Qt6::Network
    ${Protobuf_LITE_LIBRARIES}
)

include(GNUInstallDirs)
install(TARGETS hazkey-settings
    RUNTIME DESTINATION ${CMAKE_INSTALL_FULL_LIBDIR}/hazkey
)

# install desktop entry
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/hazkey-settings.desktop"
    DESTINATION ${CMAKE_INSTALL_FULL_DATADIR}/applications)

# create a symlink in the binary directory
install(CODE "
    file(MAKE_DIRECTORY \"\$ENV{DESTDIR}${CMAKE_INSTALL_FULL_BINDIR}\")
    file(CREATE_LINK
    \"${CMAKE_INSTALL_FULL_LIBDIR}/hazkey/hazkey-settings\"
    \"\$ENV{DESTDIR}${CMAKE_INSTALL_FULL_BINDIR}/hazkey-settings\"
    SYMBOLIC)
")
