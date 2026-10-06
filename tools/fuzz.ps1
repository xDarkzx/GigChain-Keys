# Fuzzes every reader of outside data (tests\fuzz) for a while each:
#   tools\fuzz.ps1                 5 minutes per fuzzer
#   tools\fuzz.ps1 -Seconds 3600 -Only fuzz_chart
# Inputs that crash, hang (over 10 s) or break a promise are saved to
# build\fuzz\crashes\<fuzzer>\; replay one with
#   build\fuzz\<fuzzer>.exe <file>
# and, once fixed, copy it into tests\fuzz\corpus\<fuzzer>\ so ctest keeps
# it fixed. New interesting inputs grow build\fuzz\corpus\ between runs.
param([int]$Seconds = 300, [string]$Only = '')
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
Set-Location $root
if (-not $env:VCPKG_ROOT) { $env:VCPKG_ROOT = Join-Path (Split-Path (Split-Path $PSScriptRoot -Parent) -Parent) 'vcpkg' } # (next to the project)
if (-not $env:QT_ROOT_DIR) { $env:QT_ROOT_DIR = 'C:\Qt\6.10.2\msvc2022_64' }
$vsInstaller = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer"
if (-not $env:VSCMD_VER) {
    $vs = & "$vsInstaller\vswhere.exe" -latest -products * -property installationPath
    $env:PATH = "$vsInstaller;$env:PATH"
    & "$vs\Common7\Tools\Launch-VsDevShell.ps1" -Arch amd64 -HostArch amd64 -SkipAutomaticLocation | Out-Null
    Set-Location $root
}

cmake --preset fuzz
if ($LASTEXITCODE) { throw 'configure failed' }
cmake --build --preset fuzz --target fuzz_setlist_json fuzz_chart fuzz_plugin_state fuzz_plugin_files fuzz_midi fuzz_chord_follow
if ($LASTEXITCODE) { throw 'build failed' }
$env:PATH = "$env:QT_ROOT_DIR\bin;$env:PATH"

$build = Join-Path $root 'build\fuzz'
$failed = @()
foreach ($seeds in Get-ChildItem (Join-Path $root 'tests\fuzz\corpus') -Directory) {
    $name = $seeds.Name
    if ($Only -and $name -ne $Only) { continue }
    $corpus = Join-Path $build "corpus\$name"
    $crashes = Join-Path $build "crashes\$name"
    New-Item -ItemType Directory -Force $corpus, $crashes | Out-Null
    Write-Output "== $name for $Seconds s"
    # libFuzzer reports on stderr: that is its output, not a failure. All of
    # it is kept in build\fuzz\<fuzzer>.log.
    $log = Join-Path $build "$name.log"
    $ErrorActionPreference = 'Continue'
    & (Join-Path $build "$name.exe") $corpus $seeds.FullName "-max_total_time=$Seconds" '-timeout=10' '-rss_limit_mb=4096' `
        "-artifact_prefix=$crashes\" '-print_final_stats=1' 2>&1 | ForEach-Object { "$_" } | Out-File $log -Encoding utf8
    $code = $LASTEXITCODE
    $ErrorActionPreference = 'Stop'
    Get-Content $log | Where-Object { $_ -match '^Done|stat::number_of_executed_units' }
    if ($code) {
        $failed += $name
        Write-Output "$name stopped with exit code $code; the end of $log :"
        Get-Content $log -Tail 25
    }
}
if ($failed) { throw "Fuzzers found problems: $($failed -join ', ') (inputs in build\fuzz\crashes)" }
Write-Output 'No fuzzer found a problem.'
