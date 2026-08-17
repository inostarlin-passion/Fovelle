#!/usr/bin/env pwsh

param (
    $Prefix = "/usr"
)

$qtVersion = [version](qmake -query QT_VERSION)
Write-Host "Detected Qt version $qtVersion"

if (-not $IsMacOS) {
    throw "Fovelle supports macOS only."
}

$argDeviceArchs =
    $env:buildArch -eq 'X64' ? 'QMAKE_APPLE_DEVICE_ARCHS=x86_64' :
    $env:buildArch -eq 'Arm64' ? 'QMAKE_APPLE_DEVICE_ARCHS=arm64' :
    $env:buildArch -eq 'Universal' ? 'QMAKE_APPLE_DEVICE_ARCHS=x86_64 arm64' :
    $null

qmake PREFIX="$Prefix" DEFINES+="$env:nightlyDefines" $argDeviceArchs
make
