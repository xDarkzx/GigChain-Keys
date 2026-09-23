# Runs the built OpenStage app with Qt's DLLs on PATH.
param([ValidateSet('debug', 'release')][string]$Preset = 'debug')
$ErrorActionPreference = 'Stop'

if (-not $env:QT_ROOT_DIR) { $env:QT_ROOT_DIR = 'C:\Qt\6.10.2\msvc2022_64' }
$root = Split-Path $PSScriptRoot -Parent
$exe = Join-Path $root "build\$Preset\openstage.exe"
if (-not (Test-Path $exe)) { throw "Not built yet: run tools\build.ps1 -Preset $Preset first" }

$env:PATH = "$env:QT_ROOT_DIR\bin;$env:PATH"
Start-Process -FilePath $exe -WorkingDirectory (Split-Path $exe)
