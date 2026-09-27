# Runs the built app with Qt's DLLs on PATH. The .exe name comes from branding.cmake.
# -Setlist opens that setlist file at start.
param([ValidateSet('debug', 'release')][string]$Preset = 'debug', [string]$Setlist = '')
$ErrorActionPreference = 'Stop'

if (-not $env:QT_ROOT_DIR) { $env:QT_ROOT_DIR = 'C:\Qt\6.10.2\msvc2022_64' }
$root = Split-Path $PSScriptRoot -Parent
$branding = Get-Content (Join-Path $root 'branding.cmake') -Raw
if ($branding -notmatch 'set\(PRODUCT_EXECUTABLE "([^"]+)"\)') { throw 'PRODUCT_EXECUTABLE not found in branding.cmake' }
$exe = Join-Path $root "build\$Preset\$($Matches[1]).exe"
if (-not (Test-Path $exe)) { throw "Not built yet: run tools\build.ps1 -Preset $Preset first ($exe)" }

$env:PATH = "$env:QT_ROOT_DIR\bin;$env:PATH"
if ($Setlist) {
    Start-Process -FilePath $exe -WorkingDirectory (Split-Path $exe) -ArgumentList "`"$((Resolve-Path $Setlist).Path)`""
} else {
    Start-Process -FilePath $exe -WorkingDirectory (Split-Path $exe)
}
