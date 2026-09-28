# Runs the soak (tests\soak): a long silent gig on the real engine, checking
# that memory, handles and threads stay flat and the audio never drops out.
#   tools\soak.ps1                      30 minutes, Release build
#   tools\soak.ps1 -Minutes 240 -Preset asan
# Readings every 10 s go to build\<preset>\soak.csv; the log to soak.log.
param([double]$Minutes = 30, [ValidateSet('release', 'debug', 'asan')][string]$Preset = 'release')
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
Set-Location $root
if (-not $env:VCPKG_ROOT) { $env:VCPKG_ROOT = 'D:\DansProject\vcpkg' }
if (-not $env:QT_ROOT_DIR) { $env:QT_ROOT_DIR = 'C:\Qt\6.10.2\msvc2022_64' }
$vsInstaller = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer"
if (-not $env:VSCMD_VER) {
    $vs = & "$vsInstaller\vswhere.exe" -latest -products * -property installationPath
    $env:PATH = "$vsInstaller;$env:PATH"
    & "$vs\Common7\Tools\Launch-VsDevShell.ps1" -Arch amd64 -HostArch amd64 -SkipAutomaticLocation | Out-Null
    Set-Location $root
}
cmake --preset $Preset
if ($LASTEXITCODE) { throw 'configure failed' }
cmake --build --preset $Preset --target gigchain_soak
if ($LASTEXITCODE) { throw 'build failed' }

$build = Join-Path $root "build\$Preset"
$env:PATH = "$env:QT_ROOT_DIR\bin;$env:PATH"
$csv = Join-Path $build 'soak.csv'
$log = Join-Path $build 'soak.log'
Write-Output "Soaking for $Minutes minutes ($Preset); readings in $csv"
$ErrorActionPreference = 'Continue' # its report is on stderr
& (Join-Path $build 'gigchain_soak.exe') $Minutes $csv 2>&1 | ForEach-Object { "$_" } | Out-File $log -Encoding utf8
$code = $LASTEXITCODE
Get-Content $log | Where-Object { $_ -match '^(playing|after warm-up|handles:|SOAK|  )' }
if ($code -ne 0) { throw "The soak failed (exit $code); see $log" }
