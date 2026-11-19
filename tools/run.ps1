# Runs the built app with Qt's DLLs on PATH. The .exe name comes from branding.cmake.
# -Setlist opens that setlist file at start.
# -DevControl (the dev preset only) starts the live test control
# (docs/testing/live-test-control.md).
param([ValidateSet('debug', 'release', 'dev')][string]$Preset = 'debug', [string]$Setlist = '', [switch]$DevControl)
$ErrorActionPreference = 'Stop'

if (-not $env:QT_ROOT_DIR) { $env:QT_ROOT_DIR = 'C:\Qt\6.10.2\msvc2022_64' }
$root = Split-Path $PSScriptRoot -Parent
$branding = Get-Content (Join-Path $root 'branding.cmake') -Raw
if ($branding -notmatch 'set\(PRODUCT_EXECUTABLE "([^"]+)"\)') { throw 'PRODUCT_EXECUTABLE not found in branding.cmake' }
$exe = Join-Path $root "build\$Preset\$($Matches[1]).exe"
if (-not (Test-Path $exe)) { throw "Not built yet: run tools\build.ps1 -Preset $Preset first ($exe)" }
if ($DevControl -and $Preset -ne 'dev') { throw '-DevControl needs the dev build (-Preset dev): no other build has the live test control' }

$env:PATH = "$env:QT_ROOT_DIR\bin;$env:PATH"
$arguments = @()
if ($DevControl) { $arguments += '--dev-control' }
if ($Setlist) { $arguments += "`"$((Resolve-Path $Setlist).Path)`"" }
if ($arguments) {
    Start-Process -FilePath $exe -WorkingDirectory (Split-Path $exe) -ArgumentList $arguments
} else {
    Start-Process -FilePath $exe -WorkingDirectory (Split-Path $exe)
}
