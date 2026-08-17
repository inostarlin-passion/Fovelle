#!/usr/bin/env pwsh

$qtVersion = [version](qmake -query QT_VERSION)
Write-Host "Detected Qt version $qtVersion"

if (-not $IsMacOS) {
    throw "Fovelle supports macOS only."
}

$binaryBaseUrl = "https://github.com/jdpurcell/kimageformats-binaries/releases/download/cont"

$pluginNames = @('QtApng', 'KImageFormats')

foreach ($pluginName in $pluginNames) {
    $artifactName = "$pluginName-macOS-$qtVersion-$env:buildArch.zip"
    $downloadUrl = "$binaryBaseUrl/$artifactName"

    Write-Host "Downloading $downloadUrl"
    Invoke-WebRequest -Uri $downloadUrl -OutFile $artifactName
    Expand-Archive $artifactName -DestinationPath $pluginName
    Remove-Item $artifactName
}

$out_frm = "bin/Fovelle.app/Contents/Frameworks"
$out_imf = "bin/Fovelle.app/Contents/PlugIns/imageformats"

New-Item -Type Directory -Path $out_frm -Force
New-Item -Type Directory -Path $out_imf -Force

function MoveLibraries($category, $destDir, $files) {
    foreach ($file in $files) {
        Write-Host "${category}: $($file.Name) ($($file.LastWriteTimeUtc.ToString("yyyy-MM-dd HH:mm:ss")))"
        Move-Item -Path $file.FullName -Destination $destDir
    }
}

# Deploy QtApng
if ($pluginNames -contains 'QtApng') {
    Write-Host "`nDeploying QtApng:"
    MoveLibraries 'imf' $out_imf (Get-ChildItem "QtApng")
}

# Deploy KImageFormats
if ($pluginNames -contains 'KImageFormats') {
    Write-Host "`nDeploying KImageFormats:"
    MoveLibraries 'imf' $out_imf (Get-ChildItem "KImageFormats" -Filter "kimg_*")
    MoveLibraries 'frm' $out_frm (Get-ChildItem "KImageFormats")
}

Write-Host ''
