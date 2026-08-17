#!/usr/bin/env pwsh

param
(
    [switch]$CI,
    $Prefix = "/usr"
)

$qtVersion = [version](qmake -query QT_VERSION)
Write-Host "Detected Qt version $qtVersion"

if (-not $IsMacOS) {
    throw "Fovelle supports macOS only."
}

# By default CMake sets the deployment target for macOS to the build machine's version; set
# it explicitly to the version supported by Qt for compatibility with older macOS versions.
$env:MACOSX_DEPLOYMENT_TARGET = (Select-String -Path (Join-Path $env:QT_ROOT_DIR 'mkspecs/qconfig.pri'), (Join-Path $env:QT_ROOT_DIR 'mkspecs/common/macx.conf') -Pattern '^\s*QMAKE_MACOSX_DEPLOYMENT_TARGET\s*=\s*(.+?)\s*$').Matches[0].Groups[1].Value

# Prepare CMake arguments
$cmakeArgs = @(
    "-DCMAKE_BUILD_TYPE=Release",
    "-DCMAKE_INSTALL_PREFIX=$Prefix"
)

if ($env:nightlyDefines) {
    $cmakeArgs += "-D$($env:nightlyDefines)"
}

if ($env:buildArch -eq 'Universal') {
    $cmakeArgs += "-DCMAKE_OSX_ARCHITECTURES=x86_64;arm64"
} elseif ($env:buildArch -eq 'Arm64') {
    $cmakeArgs += "-DCMAKE_OSX_ARCHITECTURES=arm64"
} elseif ($env:buildArch -eq 'X64') {
    $cmakeArgs += "-DCMAKE_OSX_ARCHITECTURES=x86_64"
}

# Create a build directory, configure, and build
New-Item -ItemType Directory -Force -Path build
Push-Location build
try {
    cmake $cmakeArgs ..
    cmake --build . --config Release --parallel
} finally {
    Pop-Location
}

# Copy artifact to bin directory for deployment scripts
New-Item -ItemType Directory -Force -Path bin
Copy-Item -Path "build/Fovelle.app" -Destination "bin/Fovelle.app" -Recurse -Force
