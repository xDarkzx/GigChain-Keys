# The verification gate. A change is not done, and is not committed, until
# it passes every check here:
#   1. configure + build (/W4 /WX: any compiler warning fails the build)
#   2. clang-tidy on the changed C++ files: any finding on a changed line fails
#   3. cppcheck on the same files: same rule; a file it cannot analyse fails
#   4. qmllint on the changed QML files: any finding on a changed line fails
#   5. the whole test suite (ctest); the real-plugin test must really run
# Findings on lines that were not changed are old ones: listed in the log,
# never hidden, but they do not fail the gate.
# Full output: build\verify\verify.log. Verdict: build\verify\last.json.
param([switch]$Force)
$ErrorActionPreference = 'Continue'
Set-Location (Split-Path $PSScriptRoot -Parent)
$root = (Get-Location).Path
[Environment]::CurrentDirectory = $root
$outDir = Join-Path $root 'build\verify'
New-Item -ItemType Directory -Force $outDir | Out-Null
$log = Join-Path $outDir 'verify.log'
$resultFile = Join-Path $outDir 'last.json'

function Write-Log([string]$text) { Add-Content -Path $log -Value $text -Encoding utf8 }

# ------------------------------------------------------------ what changed
$head = (git rev-parse HEAD | Out-String).Trim()
$untracked = @(git ls-files --others --exclude-standard | Where-Object { $_ })
$changed = @(@(git diff --name-only HEAD) + $untracked | Where-Object { $_ -and (Test-Path $_ -PathType Leaf) } |
    Sort-Object -Unique)
$sources = @($changed | Where-Object { $_ -match '^(src|tests|cmake|tools)/|CMakeLists\.txt$|CMakePresets\.json$|vcpkg\.json$' })

$sha = [System.Security.Cryptography.SHA256]::Create()
$material = New-Object System.Text.StringBuilder
[void]$material.AppendLine($head)
[void]$material.AppendLine((git diff HEAD | Out-String))
foreach ($file in $untracked) {
    if (Test-Path $file -PathType Leaf) {
        [void]$material.AppendLine($file)
        [void]$material.AppendLine([Convert]::ToBase64String($sha.ComputeHash([IO.File]::ReadAllBytes((Join-Path $root $file)))))
    }
}
$fingerprint = [BitConverter]::ToString($sha.ComputeHash([Text.Encoding]::UTF8.GetBytes($material.ToString()))).Replace('-', '')

if (-not $Force -and (Test-Path $resultFile)) {
    $last = Get-Content $resultFile -Raw | ConvertFrom-Json
    if ($last.fingerprint -eq $fingerprint) {
        Write-Output $last.summary
        if ($last.passed) { exit 0 } else { exit 1 }
    }
}

Set-Content -Path $log -Value "verify $(Get-Date -Format s) at $head" -Encoding utf8
Write-Log "changed files:`n  $($changed -join "`n  ")"

$checks = New-Object System.Collections.ArrayList
function Add-Check([string]$name, [bool]$passed, [string]$detail) {
    [void]$checks.Add([pscustomobject]@{ name = $name; passed = $passed; detail = $detail })
    Write-Log ("== {0}: {1} {2}" -f $name, $(if ($passed) { 'PASS' } else { 'FAIL' }), $detail)
}

function Save-Result([bool]$nothingToCheck) {
    $passed = @($checks | Where-Object { -not $_.passed }).Count -eq 0
    $lines = foreach ($c in $checks) { '[{0}] {1}: {2}' -f $(if ($c.passed) { 'PASS' } else { 'FAIL' }), $c.name, $c.detail }
    $summary = ('Verification gate {0}' -f $(if ($passed) { 'PASSED' } else { 'FAILED' })) + "`n" + ($lines -join "`n") +
        "`nFull log: build\verify\verify.log"
    [pscustomobject]@{
        fingerprint = $fingerprint; passed = $passed; nothingToCheck = $nothingToCheck
        summary = $summary; time = (Get-Date -Format s)
    } | ConvertTo-Json | Set-Content -Path $resultFile -Encoding utf8
    Write-Log $summary
    Write-Output $summary
    if ($passed) { exit 0 } else { exit 1 }
}

if ($sources.Count -eq 0) {
    Add-Check 'changes' $true 'no source changes to check'
    Save-Result $true
}

# Lines added or changed against HEAD, per file ('ALL' for a new file).
$changedLines = @{}
foreach ($file in $changed) {
    if ($untracked -contains $file) { $changedLines[$file] = 'ALL'; continue }
    $set = New-Object 'System.Collections.Generic.HashSet[int]'
    foreach ($line in (git diff -U0 HEAD -- $file)) {
        if ($line -match '^@@ -\d+(?:,\d+)? \+(\d+)(?:,(\d+))? @@') {
            $start = [int]$Matches[1]
            $count = if ($Matches[2]) { [int]$Matches[2] } else { 1 }
            for ($i = $start; $i -lt $start + $count; $i++) { [void]$set.Add($i) }
        }
    }
    $changedLines[$file] = $set
}

function ConvertTo-Relative([string]$path) {
    $full = [IO.Path]::GetFullPath($path)
    if (-not $full.StartsWith($root, [StringComparison]::OrdinalIgnoreCase)) { return $null }
    return $full.Substring($root.Length).TrimStart('\', '/').Replace('\', '/')
}

function Test-ChangedLine([string]$path, [int]$line) {
    $rel = ConvertTo-Relative $path
    if ($null -eq $rel -or -not $changedLines.ContainsKey($rel)) { return $false }
    $lines = $changedLines[$rel]
    return ($lines -eq 'ALL') -or $lines.Contains($line)
}

# ------------------------------------------------------------ environment
if (-not $env:VCPKG_ROOT) { $env:VCPKG_ROOT = 'D:\DansProject\vcpkg' }
if (-not $env:QT_ROOT_DIR) { $env:QT_ROOT_DIR = 'C:\Qt\6.10.2\msvc2022_64' }
$vsInstaller = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer"
$vs = & "$vsInstaller\vswhere.exe" -latest -products * -property installationPath
if (-not $env:VSCMD_VER) {
    $env:PATH = "$vsInstaller;$env:PATH" # the dev shell looks for vswhere on PATH
    & "$vs\Common7\Tools\Launch-VsDevShell.ps1" -Arch amd64 -HostArch amd64 -SkipAutomaticLocation | Out-Null
    Set-Location $root
}

function Invoke-Logged([string]$name, [string]$exe, [string[]]$arguments) {
    $file = Join-Path $outDir "$name.log"
    & $exe @arguments *>&1 | ForEach-Object { "$_" } | Out-File -FilePath $file -Encoding utf8
    $code = $LASTEXITCODE
    Write-Log "---- $name ($exe $($arguments -join ' ')) exit $code"
    Get-Content $file | Add-Content -Path $log -Encoding utf8
    return [pscustomobject]@{ Code = $code; File = $file }
}

function Get-Tail([string]$file, [int]$count) { (Get-Content $file -Tail $count) -join "`n" }

# ------------------------------------------------------------ 1. build
# The app's exe is locked while it runs.
Get-Process | Where-Object { $_.Path -and $_.Path.StartsWith((Join-Path $root 'build'), [StringComparison]::OrdinalIgnoreCase) } |
    Stop-Process -Force

$configure = Invoke-Logged 'configure' 'cmake' @('--preset', 'debug')
$built = $false
if ($configure.Code -ne 0) {
    Add-Check 'configure' $false ("cmake --preset debug failed:`n" + (Get-Tail $configure.File 15))
} else {
    $build = Invoke-Logged 'build' 'cmake' @('--build', '--preset', 'debug')
    $built = $build.Code -eq 0
    if ($built) { Add-Check 'build (/W4 /WX)' $true 'no errors, no warnings' }
    else {
        $errors = @(Get-Content $build.File | Where-Object { $_ -match ': (fatal )?error |: warning C\d+|error LNK' } | Select-Object -First 20)
        Add-Check 'build (/W4 /WX)' $false ("build failed:`n" + $(if ($errors) { $errors -join "`n" } else { Get-Tail $build.File 20 }))
    }
}

# ------------------------------------------------------------ 2-3. C++ lint
$compileDb = Join-Path $root 'build\debug\compile_commands.json'
$compiled = if (Test-Path $compileDb) { Get-Content $compileDb -Raw } else { '' }
$cppFiles = New-Object System.Collections.Generic.List[string]
foreach ($file in $sources) {
    $candidates = @()
    if ($file -match '\.cpp$') { $candidates = @($file) }
    elseif ($file -match '\.h$') { $candidates = @(($file -replace '\.h$', '.cpp')) }
    foreach ($c in $candidates) {
        if ((Test-Path $c) -and $compiled.Contains((Join-Path $root $c).Replace('\', '/')) -and -not $cppFiles.Contains($c)) {
            $cppFiles.Add($c)
        }
    }
}
$finding = '^(?<file>(?:[A-Za-z]:)?[^:]+):(?<line>\d+):(?<col>\d+): (?<sev>[a-z]+): (?<msg>.*)$'

if ($cppFiles.Count -eq 0) {
    Add-Check 'clang-tidy' $true 'no changed C++ files'
    Add-Check 'cppcheck' $true 'no changed C++ files'
} else {
    $tidyExe = Join-Path $vs 'VC\Tools\Llvm\x64\bin\clang-tidy.exe'
    $tidy = Invoke-Logged 'clang-tidy' $tidyExe (@('-p', 'build\debug', '--quiet') + $cppFiles)
    $new = @(); $old = 0; $broken = @()
    foreach ($line in (Get-Content $tidy.File)) {
        if ($line -notmatch $finding -or $Matches.sev -eq 'note') { continue }
        if ($Matches.sev -eq 'error') { $broken += $line; continue } # could not analyse: the check is void
        if (Test-ChangedLine $Matches.file ([int]$Matches.line)) { $new += $line } else { $old++ }
    }
    $ok = $new.Count -eq 0 -and $broken.Count -eq 0
    Add-Check 'clang-tidy' $ok ("{0} file(s); {1} finding(s) on changed lines, {2} error(s), {3} old finding(s) elsewhere{4}" -f
        $cppFiles.Count, $new.Count, $broken.Count, $old, $(if ($ok) { '' } else { "`n" + (@($broken + $new) | Select-Object -First 25) -join "`n" }))

    $cppcheckExe = 'C:\Program Files\Cppcheck\cppcheck.exe'
    if (-not (Test-Path $cppcheckExe)) {
        Add-Check 'cppcheck' $false "cppcheck is not installed ($cppcheckExe)"
    } else {
        $arguments = @("--project=$compileDb", '--enable=warning,style,performance,portability', '--inline-suppr',
            '--library=qt', '--library=windows', "--library=$root\tools\cppcheck\vst3.cfg",
            '--suppress=missingIncludeSystem', '--suppress=unmatchedSuppression', '-q', '-j', '8',
            '--template={file}:{line}:{column}: {severity}: {message} [{id}]')
        foreach ($c in $cppFiles) { $arguments += "--file-filter=*$c" }
        $check = Invoke-Logged 'cppcheck' $cppcheckExe $arguments
        $new = @(); $old = 0; $broken = @()
        foreach ($line in (Get-Content $check.File)) {
            if ($line -notmatch $finding -or $Matches.sev -eq 'information') { continue }
            if ($Matches.msg -match '\[(unknownMacro|syntaxError|internalAstError|cppcheckError|preprocessorErrorDirective)\]$') {
                $broken += $line; continue # could not analyse the file
            }
            if (Test-ChangedLine $Matches.file ([int]$Matches.line)) { $new += $line } else { $old++ }
        }
        $ok = $check.Code -eq 0 -and $new.Count -eq 0 -and $broken.Count -eq 0
        Add-Check 'cppcheck' $ok ("{0} file(s); {1} finding(s) on changed lines, {2} unanalysable, {3} old finding(s) elsewhere, exit {4}{5}" -f
            $cppFiles.Count, $new.Count, $broken.Count, $old, $check.Code, $(if ($ok) { '' } else { "`n" + (@($broken + $new) | Select-Object -First 25) -join "`n" }))
    }
}

# ------------------------------------------------------------ 4. QML lint
$qmlFiles = @($sources | Where-Object { $_ -match '\.qml$' })
if ($qmlFiles.Count -eq 0) {
    Add-Check 'qmllint' $true 'no changed QML files'
} else {
    $qmllint = Join-Path $env:QT_ROOT_DIR 'bin\qmllint.exe'
    $lint = Invoke-Logged 'qmllint' $qmllint (@('-I', 'build\debug\src\ui\qml', '-I', 'build\debug\src\ui') + $qmlFiles)
    $new = @(); $old = 0
    foreach ($line in (Get-Content $lint.File)) {
        if ($line -notmatch '^(?<sev>Warning|Error|Critical): (?<file>(?:[A-Za-z]:)?[^:]+):(?<line>\d+):(?<col>\d+): (?<msg>.*)$') { continue }
        if (Test-ChangedLine $Matches.file ([int]$Matches.line)) { $new += $line } else { $old++ }
    }
    Add-Check 'qmllint' ($new.Count -eq 0) ("{0} file(s); {1} finding(s) on changed lines, {2} old finding(s) elsewhere{3}" -f
        $qmlFiles.Count, $new.Count, $old, $(if ($new.Count) { "`n" + ($new | Select-Object -First 25) -join "`n" } else { '' }))
}

# ------------------------------------------------------------ 5. tests
if (-not $built) {
    Add-Check 'tests' $false 'not run: the build failed'
} else {
    $tests = Invoke-Logged 'ctest' 'ctest' @('--preset', 'debug', '-V')
    $failed = @(Get-Content $tests.File | Where-Object { $_ -match '^\s*\d+ - .*\((Failed|SEGFAULT|Timeout|Exception|Not Run|Subprocess aborted)' })
    $skips = @(Get-Content $tests.File | Where-Object { $_ -match 'SKIP\s+:' })
    $total = (Get-Content $tests.File | Where-Object { $_ -match 'tests passed, \d+ tests failed out of \d+' } | Select-Object -Last 1)
    $pluginViewRan = @(Get-Content $tests.File | Where-Object { $_ -match 'PASS\s+: TestPluginView::' }).Count -gt 0
    $pluginViewSkipped = @($skips | Where-Object { $_ -match 'TestPluginView::' }).Count -gt 0
    $ok = $tests.Code -eq 0 -and $failed.Count -eq 0 -and $pluginViewRan -and -not $pluginViewSkipped
    $detail = "$total"
    if ($failed.Count) { $detail += "`n" + ($failed -join "`n") }
    if (-not $pluginViewRan -or $pluginViewSkipped) { $detail += "`nthe real-plugin test (tst_plugin_view) did not run to completion" }
    if ($skips.Count) { $detail += "`nskipped: " + (($skips | ForEach-Object { $_.Trim() }) -join '; ') }
    if (-not $ok -and $tests.Code -ne 0 -and -not $failed.Count) { $detail += "`n" + (Get-Tail $tests.File 20) }
    Add-Check 'tests (ctest, every test)' $ok $detail
}

Save-Result $false
