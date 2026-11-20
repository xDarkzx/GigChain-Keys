# Runs every live check (tests/live/*.txt) against the dev build and writes a
# report: the fixes checked in the real, running app, measured
# (docs/testing/live-test-control.md). Meant to run every night on the
# development PC (the checks need its plugins and sound device; GitHub's
# machines have neither):
#   tools\live-checks.ps1            the checks, on this checkout as it is
#   tools\live-checks.ps1 -Update    first moves this checkout to origin/main (a checkout kept for this only)
# Report: build\live\<date>.log, and a line per check in build\live\summary.log.
# Exit code: 0 every check passed, 1 one failed, 2 the build failed.
param([switch]$Update)
$ErrorActionPreference = 'Continue'
$root = Split-Path $PSScriptRoot -Parent
Set-Location $root
$outDir = Join-Path $root 'build\live'
New-Item -ItemType Directory -Force $outDir | Out-Null
$stamp = Get-Date -Format 'yyyy-MM-dd_HHmm'
$report = Join-Path $outDir "$stamp.log"
function Write-Report([string]$text) { Add-Content -Path $report -Value $text -Encoding utf8; Write-Output $text }

if ($Update) {
    git fetch --quiet origin
    git checkout --quiet --detach origin/main
    Write-Report "checkout: $(git log --oneline -1)"
}

# Never against a copy someone is using: a live check starts and quits the app.
if (Get-Process GigChainKeys -ErrorAction SilentlyContinue) {
    Write-Report 'GigChain Keys is running: the live checks need it closed (nothing was run)'
    exit 1
}

& (Join-Path $PSScriptRoot 'build.ps1') -Preset dev -NoTest *>> $report
if ($LASTEXITCODE -ne 0) {
    Write-Report 'The dev build failed: no checks run (see above)'
    Add-Content -Path (Join-Path $outDir 'summary.log') -Value "$stamp BUILD FAILED" -Encoding utf8
    exit 2
}

$env:PATH = "$(if ($env:QT_ROOT_DIR) { $env:QT_ROOT_DIR } else { 'C:\Qt\6.10.2\msvc2022_64' })\bin;$env:PATH"
$tool = Join-Path $root 'build\dev\gck-ctl.exe'
$failed = 0
foreach ($check in Get-ChildItem (Join-Path $root 'tests\live') -Filter '*.txt' | Sort-Object Name) {
    Write-Report "== $($check.Name)"
    & $tool run $check.FullName *>> $report
    $code = $LASTEXITCODE
    # (Whatever happened, the app is not left running for the next check.)
    & $tool quit discard *> $null
    Start-Sleep -Seconds 3
    Get-Process GigChainKeys -ErrorAction SilentlyContinue | Where-Object { $_.Path -like "$root\build\dev\*" } | Stop-Process -Force
    $verdict = if ($code -eq 0) { 'PASS' } else { $failed++; "FAIL (exit $code)" }
    Write-Report "   $verdict"
    Add-Content -Path (Join-Path $outDir 'summary.log') -Value "$stamp $($check.Name) $verdict" -Encoding utf8
}
Write-Report "Done: $failed failed. Report: $report"
exit $(if ($failed -gt 0) { 1 } else { 0 })
