#!/bin/bash

# Every developer build produces a runnable app. Pass this explicitly so an
# older CMakeCache.txt configured with translations disabled cannot override
# the project's current multilingual default.
CMAKE_ARGS="-DFOVELLE_BUILD_TRANSLATIONS=ON -DCMAKE_OSX_DEPLOYMENT_TARGET=15.0"

CLEAN=false

# Fovelle is a macOS-only application.
if [[ "$(uname)" != "Darwin" ]]; then
    echo "Fovelle supports macOS only." >&2
    exit 1
fi

# Find a valid macOS SDK and set it for CMake to fix a potential mismatch.
SDK_PATH=$(xcrun --sdk macosx --show-sdk-path 2>/dev/null)
if [ -n "$SDK_PATH" ]; then
    CMAKE_ARGS="$CMAKE_ARGS -DCMAKE_OSX_SYSROOT=$SDK_PATH"
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
        clang-format -i **/*.cpp **/*.h **/*.mm --dry-run -Werror
        exit 0
        ;;
        --tidy)
        # Static analysis must not run the release-only Ghostscript staging
        # hook.  The hook can build third-party C sources and requires
        # platform-specific Fontconfig headers that are not part of the
        # analysis job.  CI only needs clang-tidy on Fovelle's own targets.
        CMAKE_ARGS="$CMAKE_ARGS -DCMAKE_CXX_CLANG_TIDY=clang-tidy -DFOVELLE_BUNDLE_GHOSTSCRIPT=OFF"
        shift # Remove --tidy from processing
        ;;
        --tidy-fix)
        CMAKE_ARGS="$CMAKE_ARGS -DCMAKE_CXX_CLANG_TIDY='clang-tidy;-fix-errors' -DFOVELLE_BUNDLE_GHOSTSCRIPT=OFF"
        shift # Remove --tidy-fix from processing
        ;;
        --clean)
        CLEAN=true
        shift
        ;;
        *)
        CMAKE_ARGS="$CMAKE_ARGS $arg"
        ;;
    esac
done

# Clean build directory for a fresh configuration
if $CLEAN && [ -d "build" ]; then
    echo "Removing existing build directory."
    rm -rf build
fi

echo "Configuring with: cmake -B build -G Ninja $CMAKE_ARGS"

# Run CMake configuration.
cmake -B build $CMAKE_ARGS

# Run the build
echo "Building project..."
cmake --build build --parallel
