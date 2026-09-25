# Claude Code hook around tools\verify.ps1. Claude cannot get past a failing gate:
#   -Mode commit  (PreToolUse, Bash/PowerShell): a "git commit" is refused unless the gate passes.
#   -Mode stop    (Stop): Claude cannot end its turn with changes that fail the gate;
#                 the verdict is shown to the user whatever Claude says.
param([Parameter(Mandatory = $true)][ValidateSet('commit', 'stop')][string]$Mode)
$ErrorActionPreference = 'Continue'
[Console]::OutputEncoding = [Text.Encoding]::UTF8
$raw = [Console]::In.ReadToEnd()
$hookInput = if ($raw) { $raw | ConvertFrom-Json } else { $null }
$gate = Join-Path $PSScriptRoot 'verify.ps1'
$resultFile = Join-Path (Split-Path $PSScriptRoot -Parent) 'build\verify\last.json'

function Write-Json($value) { [Console]::Out.Write(($value | ConvertTo-Json -Compress)) }

if ($Mode -eq 'commit') {
    $command = [string]$hookInput.tool_input.command
    if ($command -notmatch '\bgit\b[^|;&]*\bcommit\b') { exit 0 }
    $summary = & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $gate 2>&1 | Out-String
    if ($LASTEXITCODE -eq 0) { exit 0 }
    [Console]::Error.WriteLine("COMMIT BLOCKED by tools\verify.ps1. Fix every failure below; do not commit or call it done.`n$summary")
    exit 2
}

# Stop
$summary = & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $gate 2>&1 | Out-String
$passed = $LASTEXITCODE -eq 0
$result = if (Test-Path $resultFile) { Get-Content $resultFile -Raw | ConvertFrom-Json } else { $null }
if ($passed -and $result -and $result.nothingToCheck) { exit 0 }
if ($passed) {
    Write-Json @{ systemMessage = $summary.Trim() }
    exit 0
}
if ($hookInput -and $hookInput.stop_hook_active) {
    # Already sent back once: let the turn end, but the user sees the failure.
    Write-Json @{ systemMessage = "The changes FAIL verification:`n" + $summary.Trim() }
    exit 0
}
Write-Json @{
    decision      = 'block'
    reason        = "tools\verify.ps1 FAILED on the current changes. Fix every failure, or tell the user plainly that it fails and why. Never say it works.`n" + $summary.Trim()
    systemMessage = 'Verification gate FAILED; Claude was sent back to fix it (build\verify\verify.log)'
}
exit 0
