#!/usr/bin/env pwsh

# This script will download binary plugins from the kimageformats-binaries repository using Github's API.

$pluginNames = "qtapng", "kimageformats"

$qtVersion = [version](qmake -query QT_VERSION)
Write-Host "Detected Qt Version $qtVersion"

if (-not $IsMacOS) {
    throw "Fovelle supports macOS only."
}

# Qt version availability and runner names are assumed.
$imageName = $qtVersion -lt [version]'6.5.3' ? "macos-13" : "macos-14"

$binaryBaseUrl = "https://github.com/jurplel/kimageformats-binaries/releases/download/cont"

if ($pluginNames.count -eq 0) {
    Write-Host "the pluginNames array is empty."
}

foreach ($pluginName in $pluginNames) {
    $qtArch = $env:qtArch ? "-$env:qtArch" : ''
    $artifactName = "$pluginName-$imageName-$qtVersion$qtArch.zip"
    $downloadUrl = "$binaryBaseUrl/$artifactName"

    Write-Host "Downloading $downloadUrl"
    Invoke-WebRequest -URI $downloadUrl -OutFile $artifactName
    Expand-Archive $artifactName -DestinationPath $pluginName
    Remove-Item $artifactName
}

$out_frm = "bin/Fovelle.app/Contents/Frameworks"
$out_imf = "bin/Fovelle.app/Contents/PlugIns/imageformats"

New-Item -Type Directory -Path "$out_frm" -Force
New-Item -Type Directory -Path "$out_imf" -Force

function MoveLibraries($category, $destDir, $files) {
    foreach ($file in $files) {
        Write-Host "${category}: $($file.Name)"
        Move-Item -Path $file.FullName -Destination $destDir
    }
}

# Deploy QtApng
if ($pluginNames -contains 'qtapng') {
    Write-Host "`nDeploying QtApng:"
    MoveLibraries 'imf' $out_imf (Get-ChildItem "qtapng/QtApng/output")
}

# Deploy KImageFormats
if ($pluginNames -contains 'kimageformats') {
    Write-Host "`nDeploying KImageFormats:"
    MoveLibraries 'imf' $out_imf (Get-ChildItem "kimageformats/kimageformats/output" -Filter "kimg_*")
    MoveLibraries 'frm' $out_frm (Get-ChildItem "kimageformats/kimageformats/output")
}

Write-Host ''
