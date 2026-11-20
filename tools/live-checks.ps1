# Runs every live check (tests/live/*.txt) against the dev build and writes a
# report: the fixes checked in the real, running app, measured
# (docs/testing/live-test-control.md). Meant to run every night on the
# development PC (the checks need its plugins and sound device; GitHub's
# machines have neither):
#   tools\live-checks.ps1            the checks, on this checkout as it is
#   tools\live-checks.ps1 -Update    first moves this checkout to origin/main (a checkout kept for this only)
# Report: build\live\<date>.log, and a line per check in build\live\summary.log.
# Exit code: 0 every check passed, 1 one failed (or none were found), 2 the
# checkout or the build failed.
param([switch]$Update)
$ErrorActionPreference = 'Continue'
$root = Split-Path $PSScriptRoot -Parent
Set-Location $root
$outDir = Join-Path $root 'build\live'
New-Item -ItemType Directory -Force $outDir | Out-Null
$stamp = Get-Date -Format 'yyyy-MM-dd_HHmm'
$report = Join-Path $outDir "$stamp.log"
function Write-Report([string]$text) { Add-Content -Path $report -Value $text -Encoding utf8; Write-Output $text }
# Never against a copy someone is using: a live check starts and quits the app.
# (Checked before the build, and again before every check: an app opened
# meanwhile is never driven.)
function Test-Closed {
    if (-not (Get-Process GigChainKeys -ErrorAction SilentlyContinue)) { return $true }
    Write-Report 'GigChain Keys is running: the live checks need it closed (the rest were not run)'
    return $false
}

if ($Update) {
    # (Each checked: a failed fetch or checkout would test an older revision.)
    git fetch --quiet origin
    if ($LASTEXITCODE -ne 0) { Write-Report "git fetch failed (exit $LASTEXITCODE): no checks run"; exit 2 }
    git checkout --quiet --detach origin/main
    if ($LASTEXITCODE -ne 0) { Write-Report "git checkout origin/main failed (exit $LASTEXITCODE): no checks run"; exit 2 }
    Write-Report "checkout: $(git log --oneline -1)"
}

if (-not (Test-Closed)) { exit 1 }

# In its own process: its exit code is the build's, whatever went wrong (a
# PowerShell error in the script included), never an earlier command's. Its
# output goes into the report as UTF-8, like the rest.
$built = cmd /c "powershell -NoProfile -ExecutionPolicy Bypass -File `"$PSScriptRoot\build.ps1`" -Preset dev -NoTest 2>&1"
$buildCode = $LASTEXITCODE
Add-Content -Path $report -Value $built -Encoding utf8
if ($buildCode -ne 0) {
    Write-Report 'The dev build failed: no checks run (see above)'
    Add-Content -Path (Join-Path $outDir 'summary.log') -Value "$stamp BUILD FAILED" -Encoding utf8
    exit 2
}

$env:PATH = "$(if ($env:QT_ROOT_DIR) { $env:QT_ROOT_DIR } else { 'C:\Qt\6.10.2\msvc2022_64' })\bin;$env:PATH"
$tool = Join-Path $root 'build\dev\gck-ctl.exe'
# No checks found is a failure: a missing folder must never read as "all passed".
try { $checks = @(Get-ChildItem (Join-Path $root 'tests\live') -Filter '*.txt' -ErrorAction Stop | Sort-Object Name) }
catch { Write-Report "The live checks could not be listed: $_"; exit 1 }
if ($checks.Count -eq 0) { Write-Report 'No live checks in tests\live: nothing was checked'; exit 1 }
$failed = 0
$ours = Join-Path $root 'build\dev\*'
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8 # (gck-ctl prints UTF-8: plugin names, "—")
foreach ($check in $checks) {
    if (-not (Test-Closed)) { exit 1 }
    Write-Report "== $($check.Name)"
    $said = cmd /c "`"$tool`" run `"$($check.FullName)`" 2>&1"
    $code = $LASTEXITCODE
    Add-Content -Path $report -Value $said -Encoding utf8
    # (Whatever happened, this run's app is not left running for the next check:
    # only this checkout's dev build is ended, never another copy.)
    Start-Sleep -Seconds 3
    Get-Process GigChainKeys -ErrorAction SilentlyContinue | Where-Object { $_.Path -like $ours } | Stop-Process -Force
    $verdict = if ($code -eq 0) { 'PASS' } else { $failed++; "FAIL (exit $code)" }
    Write-Report "   $verdict"
    Add-Content -Path (Join-Path $outDir 'summary.log') -Value "$stamp $($check.Name) $verdict" -Encoding utf8
}
Write-Report "Done: $failed failed. Report: $report"
exit $(if ($failed -gt 0) { 1 } else { 0 })
