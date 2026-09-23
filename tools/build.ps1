# Configure, build and test OpenStage from any PowerShell prompt.
param(
    [ValidateSet('debug', 'release', 'asan')][string]$Preset = 'debug',
    [string]$Filter = '',
    [switch]$NoTest
)
$ErrorActionPreference = 'Stop'

if (-not $env:VCPKG_ROOT) { $env:VCPKG_ROOT = 'D:\DansProject\vcpkg' }
if (-not $env:QT_ROOT_DIR) { $env:QT_ROOT_DIR = 'C:\Qt\6.10.2\msvc2022_64' }

if (-not $env:VSCMD_VER) {
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    $vs = & $vswhere -latest -products * -property installationPath
    & "$vs\Common7\Tools\Launch-VsDevShell.ps1" -Arch amd64 -HostArch amd64 -SkipAutomaticLocation | Out-Null
}

Set-Location (Split-Path $PSScriptRoot -Parent)

cmake --preset $Preset
if ($LASTEXITCODE) { exit $LASTEXITCODE }
cmake --build --preset $Preset
if ($LASTEXITCODE) { exit $LASTEXITCODE }
if ($NoTest) { exit 0 }

if ($Filter) { ctest --preset $Preset -R $Filter } else { ctest --preset $Preset }
exit $LASTEXITCODE
