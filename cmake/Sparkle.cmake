# Official binary distribution, pinned and checksum verified. No signing keys are downloaded.
set(FOVELLE_SPARKLE_ROOT "${CMAKE_BINARY_DIR}/_deps/sparkle" CACHE PATH "Sparkle distribution root")
set(FOVELLE_UPDATE_FEED_URL "" CACHE STRING "HTTPS Sparkle appcast URL")
set(FOVELLE_UPDATE_PUBLIC_KEY "" CACHE STRING "Base64 Ed25519 public key (32 bytes)")
option(FOVELLE_REQUIRE_UPDATE_CONFIG "Reject builds without release update configuration" OFF)
if(FOVELLE_REQUIRE_UPDATE_CONFIG)
    if(NOT FOVELLE_UPDATE_FEED_URL MATCHES "^https://[^/]+/" OR NOT FOVELLE_UPDATE_PUBLIC_KEY MATCHES "^[A-Za-z0-9+/]+=$")
        message(FATAL_ERROR "Release updates require HTTPS FOVELLE_UPDATE_FEED_URL and FOVELLE_UPDATE_PUBLIC_KEY")
    endif()
    string(LENGTH "${FOVELLE_UPDATE_PUBLIC_KEY}" key_length)
    if(NOT key_length EQUAL 44)
        message(FATAL_ERROR "FOVELLE_UPDATE_PUBLIC_KEY must encode a 32-byte Ed25519 key")
    endif()
endif()
# Reject XML injection even in development configuration.
if(FOVELLE_UPDATE_FEED_URL MATCHES "[<>&\"]" OR FOVELLE_UPDATE_PUBLIC_KEY MATCHES "[<>&\"]")
    message(FATAL_ERROR "Update configuration must contain XML-safe values")
endif()
if(NOT EXISTS "${FOVELLE_SPARKLE_ROOT}/Sparkle.framework")
    file(MAKE_DIRECTORY "${FOVELLE_SPARKLE_ROOT}")
    file(DOWNLOAD
        "https://github.com/sparkle-project/Sparkle/releases/download/2.10.0/Sparkle-2.10.0.tar.xz"
        "${FOVELLE_SPARKLE_ROOT}/Sparkle.tar.xz"
        EXPECTED_HASH SHA256=c2bf58aa8387266ac179357b1415d6f2635f044da8be41042af32425dae6da0c
        TLS_VERIFY ON)
    execute_process(COMMAND tar -xf "${FOVELLE_SPARKLE_ROOT}/Sparkle.tar.xz" -C "${FOVELLE_SPARKLE_ROOT}"
        RESULT_VARIABLE unpack_result)
    if(NOT unpack_result EQUAL 0)
        message(FATAL_ERROR "Cannot unpack Sparkle")
    endif()
endif()
find_library(FOVELLE_SPARKLE_FRAMEWORK Sparkle PATHS "${FOVELLE_SPARKLE_ROOT}" NO_DEFAULT_PATH REQUIRED)
function(fovelle_link_sparkle target)
    target_sources(${target} PRIVATE "${PROJECT_SOURCE_DIR}/src/updatechecker_sparkle.mm")
    set_source_files_properties("${PROJECT_SOURCE_DIR}/src/updatechecker_sparkle.mm" PROPERTIES COMPILE_FLAGS "-fobjc-arc")
    target_link_libraries(${target} PRIVATE "${FOVELLE_SPARKLE_FRAMEWORK}")
    set_property(TARGET ${target} APPEND PROPERTY BUILD_RPATH "${FOVELLE_SPARKLE_ROOT}" "@executable_path/../Frameworks")
endfunction()
function(fovelle_embed_sparkle target)
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_BUNDLE_DIR:${target}>/Contents/Frameworks"
        COMMAND /usr/bin/ditto "${FOVELLE_SPARKLE_FRAMEWORK}" "$<TARGET_BUNDLE_DIR:${target}>/Contents/Frameworks/Sparkle.framework"
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_BUNDLE_DIR:${target}>/Contents/Resources/licenses/Sparkle"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${PROJECT_SOURCE_DIR}/third_party/sparkle/LICENSE"
            "$<TARGET_BUNDLE_DIR:${target}>/Contents/Resources/licenses/Sparkle/LICENSE"
        VERBATIM)
endfunction()
