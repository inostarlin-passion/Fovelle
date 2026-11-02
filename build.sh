#!/bin/bash

# Every developer build produces a runnable app. Pass this explicitly so an
# older CMakeCache.txt configured with translations disabled cannot override
# the project's current multilingual default.
CMAKE_ARGS=(-DFOVELLE_BUILD_TRANSLATIONS=ON -DCMAKE_OSX_DEPLOYMENT_TARGET=15.0)

CLEAN=false

# Fovelle is a macOS-only application.
if [[ "$(uname)" != "Darwin" ]]; then
    echo "Fovelle supports macOS only." >&2
    exit 1
fi

# Find a valid macOS SDK and set it for CMake to fix a potential mismatch.
SDK_PATH=$(xcrun --sdk macosx --show-sdk-path 2>/dev/null)
if [ -n "$SDK_PATH" ]; then
    CMAKE_ARGS+=("-DCMAKE_OSX_SYSROOT=$SDK_PATH")
fi

# Parse command-line arguments
for arg in "$@"
do
    case $arg in
        --format)
        clang-format -i **/*.cpp **/*.h **/*.mm
        exit 0
        ;;
        --format-check)
        python3 .github/scripts/check_format.py
        exit $?
        ;;
        --tidy)
        # Static analysis must not run the release-only Ghostscript staging
        # hook.  The hook can build third-party C sources and requires
        # platform-specific Fontconfig headers that are not part of the
        # analysis job.  CI only needs clang-tidy on Fovelle's own targets.
        CMAKE_ARGS+=(-DCMAKE_CXX_CLANG_TIDY=clang-tidy -DCMAKE_OBJCXX_CLANG_TIDY=clang-tidy -DFOVELLE_BUNDLE_GHOSTSCRIPT=OFF)
        ;;
        --tidy-fix)
        CMAKE_ARGS+=("-DCMAKE_CXX_CLANG_TIDY=clang-tidy;-fix-errors" "-DCMAKE_OBJCXX_CLANG_TIDY=clang-tidy;-fix-errors" -DFOVELLE_BUNDLE_GHOSTSCRIPT=OFF)
        ;;
        --clean)
        CLEAN=true
        ;;
        *)
        CMAKE_ARGS+=("$arg")
        ;;
    esac
done

# Clean build directory for a fresh configuration
if $CLEAN && [ -d "build" ]; then
    echo "Removing existing build directory."
    rm -rf build
fi

printf 'Configuring with: cmake -B build'
printf ' %q' "${CMAKE_ARGS[@]}"
printf '\n'

# Run CMake configuration.
cmake -B build "${CMAKE_ARGS[@]}"
configure_status=$?
if [ "$configure_status" -ne 0 ]; then
    exit "$configure_status"
fi

# Run the build
echo "Building project..."
cmake --build build --parallel
