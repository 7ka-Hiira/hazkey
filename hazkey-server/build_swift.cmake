set(SWIFT_COMMAND
    "${SWIFT_EXECUTABLE}"
    "build" "-c" "${SWIFT_BUILD_TYPE}"
    "--scratch-path=${CMAKE_CURRENT_BINARY_DIR}/swift-build"
)

if(HAZKEY_SERVER_SWIFT_SDK)
    list(APPEND SWIFT_COMMAND "--swift-sdk" "${HAZKEY_SERVER_SWIFT_SDK}")
endif()

if(HAZKEY_SERVER_ZENZAI_TRAIT)
    list(APPEND SWIFT_COMMAND "--traits" "ZenzaiSupport")
    list(APPEND SWIFT_COMMAND "-Xlinker" "-L${LIBLLAMA_DIR}")
endif()

if(HAZKEY_SERVER_SWIFT_LTO_MODE)
    list(APPEND SWIFT_COMMAND "--experimental-lto-mode" "${HAZKEY_SERVER_SWIFT_LTO_MODE}")
endif()

if(SWIFT_STATIC_STDLIB)
    list(APPEND SWIFT_COMMAND "-Xswiftc" "-static-stdlib")

    if(SWIFT_DYNAMIC_LIB_PATH)
        list(APPEND SWIFT_COMMAND "-Xlinker" "-L${SWIFT_DYNAMIC_LIB_PATH}")
    endif()
endif()

if(SWIFT_DISABLE_DEPENDENCY_CACHE)
    list(APPEND SWIFT_COMMAND "--disable-dependency-cache")
endif()

if(SWIFT_LINK_PATH)
    list(APPEND SWIFT_COMMAND "-Xlinker" "-L${SWIFT_LINK_PATH}")
endif()

execute_process(
    COMMAND ${SWIFT_COMMAND}
    WORKING_DIRECTORY "${SWIFT_WORK_DIR}"
    RESULT_VARIABLE result
)

# The first build fails for an unknown reason.
if(NOT result EQUAL 0)
    execute_process(
        COMMAND ${SWIFT_COMMAND}
        WORKING_DIRECTORY "${SWIFT_WORK_DIR}"
        RESULT_VARIABLE result2
    )
    if(NOT result2 EQUAL 0)
        message(FATAL_ERROR "Swift build failed after two attempts.")
    endif()
endif()
