using namespace System.Runtime.InteropServices

if ([RuntimeInformation]::OSArchitecture -ne [Architecture]::X64) {
    throw 'Unsupported host architecture.'
}

$argArch =
    $env:buildArch -eq 'X64' ? 'x64' :
    $env:buildArch -eq 'X86' ? 'x64_x86' :
    $env:buildArch -eq 'Arm64' ? 'x64_arm64' :
    $null
if (-not $argArch) {
    throw 'Unsupported build architecture.'
}
$vsDir = vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
$exclusions = @('VCPKG_ROOT')
cmd /c "`"$(Join-Path $vsDir 'VC\Auxiliary\Build\vcvarsall.bat')`" $argArch > null && set" | ForEach-Object {
    $name, $value = $_ -Split '=', 2
    if ($name -notin $exclusions) {
        [Environment]::SetEnvironmentVariable($name, $value)
    }
}
