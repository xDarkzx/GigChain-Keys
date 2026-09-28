# Builds what testers download: the Windows installer and a portable zip, in
# dist\. From a clean Release build, with every test run on it first:
#   tools\package.ps1            build, test, stage, check, pack
#   tools\package.ps1 -SkipTests only when the same commit was just tested
#
# Steps: Release build -> ctest -> cmake --install into build\release\stage
# (the app, its scanner, their libraries, the Visual C++ runtime, licences)
# -> windeployqt (Qt's libraries, plugins and QML) -> checks (every file
# there, exploit mitigations on our binaries, the scanner starts with only
# the staged files) -> zip + Inno Setup installer (installer\setup.iss).
param([switch]$SkipTests)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
Set-Location $root

if (-not $env:VCPKG_ROOT) { $env:VCPKG_ROOT = 'D:\DansProject\vcpkg' }
if (-not $env:QT_ROOT_DIR) { $env:QT_ROOT_DIR = 'C:\Qt\6.10.2\msvc2022_64' }
$vsInstaller = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer"
if (-not $env:VSCMD_VER) {
    $vs = & "$vsInstaller\vswhere.exe" -latest -products * -property installationPath
    $env:PATH = "$vsInstaller;$env:PATH" # the dev shell looks for vswhere on PATH
    & "$vs\Common7\Tools\Launch-VsDevShell.ps1" -Arch amd64 -HostArch amd64 -SkipAutomaticLocation | Out-Null
    Set-Location $root
}
$iscc = @("${env:LOCALAPPDATA}\Programs\Inno Setup 6\ISCC.exe", "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe") |
    Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $iscc) { throw 'Inno Setup 6 is not installed: winget install JRSoftware.InnoSetup' }

function Invoke-Step([string]$name, [scriptblock]$run) {
    Write-Output "== $name"
    & $run
    if ($LASTEXITCODE) { throw "$name failed (exit $LASTEXITCODE)" }
}

$branding = Get-Content (Join-Path $root 'branding.cmake') -Raw
function Get-Branding([string]$key) {
    if ($branding -notmatch "set\($key `"([^`"]+)`"\)") { throw "$key not found in branding.cmake" }
    return $Matches[1]
}
$exeName = Get-Branding 'PRODUCT_EXECUTABLE'
$version = Get-Branding 'PRODUCT_VERSION'

# ------------------------------------------------------------ build and test
Invoke-Step 'configure' { cmake --preset release }
Invoke-Step 'build' { cmake --build --preset release }
if (-not $SkipTests) { Invoke-Step 'tests' { ctest --preset release } }

# ------------------------------------------------------------ stage
$build = Join-Path $root 'build\release'
$stage = Join-Path $build 'stage'
if (Test-Path $stage) { Remove-Item -Recurse -Force $stage }
Invoke-Step 'install' { cmake --install $build --prefix $stage }
$app = Join-Path $stage "$exeName.exe"
Invoke-Step 'windeployqt' {
    & (Join-Path $env:QT_ROOT_DIR 'bin\windeployqt.exe') --release --qmldir (Join-Path $root 'src\ui') `
        --no-translations --no-compiler-runtime --no-opengl-sw --no-system-d3d-compiler $app
}

# ------------------------------------------------------------ checks
Write-Output '== checks'
$problems = @()
foreach ($required in "$exeName.exe", "$($exeName)Scan.exe", 'Qt6Core.dll', 'Qt6Quick.dll', 'Qt6Network.dll', 'Qt6Multimedia.dll',
    'vcruntime140.dll', 'msvcp140.dll', 'LICENSE.txt', 'THIRD-PARTY-NOTICES.txt', 'platforms\qwindows.dll', 'qml\QtQuick\Controls') {
    if (-not (Test-Path (Join-Path $stage $required))) { $problems += "missing: $required" }
}
if (-not (Get-ChildItem $stage -Filter 'rtaudio*.dll')) { $problems += 'missing: the RtAudio library' }
if (Get-ChildItem $stage -Recurse -Filter '*d.dll' | Where-Object { $_.Name -match '^(Qt6\w+d|rtaudiod)\.dll$' }) {
    $problems += 'debug libraries were staged'
}
# Our own binaries carry the exploit mitigations the build asks for.
foreach ($binary in "$exeName.exe", "$($exeName)Scan.exe") {
    $headers = (dumpbin /headers (Join-Path $stage $binary) | Out-String)
    foreach ($flag in 'Dynamic base', 'NX compatible', 'High Entropy Virtual Addresses', 'Guard') {
        if ($headers -notmatch $flag) { $problems += "$binary lacks '$flag'" }
    }
}
# The scanner starts with only the staged files (no Qt or runtime on PATH):
# it answers a missing plugin with its usage error, not a missing DLL.
$cleanPath = "$env:SystemRoot\System32;$env:SystemRoot"
$savedPath = $env:PATH
try {
    $env:PATH = $cleanPath
    $scanner = Start-Process -FilePath (Join-Path $stage "$($exeName)Scan.exe") -NoNewWindow -Wait -PassThru `
        -RedirectStandardError (Join-Path $build 'scanner-check.txt')
    if ($scanner.ExitCode -ne 2) { $problems += "the staged scanner did not start (exit $($scanner.ExitCode))" }
} finally {
    $env:PATH = $savedPath
}
if ($problems) { throw ("The staged app is not ready:`n  " + ($problems -join "`n  ")) }
Write-Output 'staged files complete; mitigations on; the scanner starts on its own'

# ------------------------------------------------------------ pack
$dist = Join-Path $root 'dist'
New-Item -ItemType Directory -Force $dist | Out-Null
$zip = Join-Path $dist "$exeName-$version-x64-portable.zip"
if (Test-Path $zip) { Remove-Item $zip }
Write-Output '== zip'
Compress-Archive -Path (Join-Path $stage '*') -DestinationPath $zip -CompressionLevel Optimal
Invoke-Step 'installer' {
    & $iscc /Qp "/J$build\installer\branding.iss" "/DStageDir=$stage" "/DOutputDir=$dist" (Join-Path $root 'installer\setup.iss')
}
$setup = Join-Path $dist "$exeName-$version-x64-setup.exe"
foreach ($file in $setup, $zip) {
    $hash = (Get-FileHash $file -Algorithm SHA256).Hash
    Write-Output ("{0}  {1:N1} MB  SHA256 {2}" -f (Split-Path $file -Leaf), ((Get-Item $file).Length / 1MB), $hash)
}
