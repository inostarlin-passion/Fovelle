include(FetchContent)
if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif()

# Build for the same architectures as Fovelle without a runtime dylib.
FetchContent_Declare(fovelle_libpng
    URL https://codeload.github.com/pnggroup/libpng/tar.gz/refs/tags/v1.6.59
    URL_HASH SHA256=2540302a1844ad2b2b501977abecfa850f265f97b78f065a712ab4074a89f5b5
    TLS_VERIFY ON)

function(fovelle_add_libpng)
    set(PNG_SHARED OFF)
    set(PNG_FRAMEWORK OFF)
    set(PNG_STATIC ON)
    set(PNG_TESTS OFF)
    # Upstream chooses SIMD sources using a single target architecture.
    if(CMAKE_OSX_ARCHITECTURES MATCHES ";")
        set(PNG_HARDWARE_OPTIMIZATIONS OFF)
    endif()
    set(PNG_TOOLS OFF)
    set(SKIP_INSTALL_ALL ON)
    # Inherited from the app directory; only Fovelle's Qt sources need them.
    set(CMAKE_AUTOMOC OFF)
    set(CMAKE_AUTOUIC OFF)
    set(CMAKE_AUTORCC OFF)
    FetchContent_MakeAvailable(fovelle_libpng)
    if(NOT PNG_HARDWARE_OPTIMIZATIONS)
        target_compile_definitions(png_static PRIVATE PNG_ARM_NEON_OPT=0 PNG_INTEL_SSE_OPT=0)
    endif()
    set(FOVELLE_LIBPNG_LICENSE "${fovelle_libpng_SOURCE_DIR}/LICENSE" PARENT_SCOPE)
endfunction()

fovelle_add_libpng()

if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.28)
    set(fovelle_webp_exclusion EXCLUDE_FROM_ALL)
endif()
FetchContent_Declare(fovelle_libwebp
    ${fovelle_webp_exclusion}
    URL https://storage.googleapis.com/downloads.webmproject.org/releases/webp/libwebp-1.6.0.tar.gz
    URL_HASH SHA256=e4ab7009bf0629fd11982d4c2aa83964cf244cffba7347ecd39019a9e38c4564
    TLS_VERIFY ON)

function(fovelle_add_libwebp)
    set(BUILD_SHARED_LIBS OFF)
    foreach(feature ANIM_UTILS CWEBP DWEBP GIF2WEBP IMG2WEBP VWEBP WEBPINFO LIBWEBPMUX WEBPMUX EXTRAS)
        set(WEBP_BUILD_${feature} OFF)
    endforeach()
    set(CMAKE_AUTOMOC OFF)
    set(CMAKE_AUTOUIC OFF)
    set(CMAKE_AUTORCC OFF)
    if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.28)
        FetchContent_MakeAvailable(fovelle_libwebp)
    else()
        FetchContent_GetProperties(fovelle_libwebp)
        if(NOT fovelle_libwebp_POPULATED)
            FetchContent_Populate(fovelle_libwebp)
            add_subdirectory("${fovelle_libwebp_SOURCE_DIR}" "${fovelle_libwebp_BINARY_DIR}" EXCLUDE_FROM_ALL)
        endif()
    endif()
    # libwebp 1.6.0's decoder target omits src from its build interface.
    target_include_directories(webpdecoder INTERFACE
        "$<BUILD_INTERFACE:${fovelle_libwebp_SOURCE_DIR}/src>")
    set(FOVELLE_LIBWEBP_NOTICES
        "${fovelle_libwebp_SOURCE_DIR}/COPYING" "${fovelle_libwebp_SOURCE_DIR}/PATENTS" PARENT_SCOPE)
endfunction()

fovelle_add_libwebp()
