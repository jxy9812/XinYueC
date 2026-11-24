# gl_round_gate.ps1 -- GPU/GL round gate: perf A/B + SW-vs-GL parity + full-GPU check.
#
# Usage:
#   powershell -NoProfile -ExecutionPolicy Bypass -File Tools\windows\gl_round_gate.ps1 -Freeze
#   powershell -NoProfile -ExecutionPolicy Bypass -File Tools\windows\gl_round_gate.ps1 [-BaseExe <path>] [-Rounds N] [-MinGainPct X]
#
# -Freeze   copy bin-relwithdebinfo\XGuiWindowDemo_Test.exe to GLbase.exe
#           (and to GLorigin.exe when that file does not exist yet), print
#           FROZEN, exit 0. Freezing turns the current build into the
#           performance baseline for the next round of changes.
# -BaseExe  baseline executable (default bin-relwithdebinfo\XGuiWindowDemo_GLbase.exe,
#           relative paths are resolved against the repo root).
# -Rounds   benchmark samples per executable (default 3, alternated base/cand).
# -MinGainPct  required CPU-time gain of candidate over baseline (default 1.5).
#
# Gate steps (non-freeze):
#   1. bench base vs XGuiWindowDemo_Test.exe: XGUI_RENDER_BACKEND=gpu,
#      XGUI_BENCH_CPU=1, "--benchmark 5 --benchmark-full --page 4 --tab 20";
#      capture fps= and cpu=<n>ms/frame, median of Rounds CPU samples each.
#   2. parity: XGuiGpuParity_Test.exe software dump + gpu dump to out\gl_gate_*.raw,
#      then --diff and capture "hard(>3/chan) px=<n>".
#   3. full-GPU: XGPU_PAINTER_PROF=1 3s GL bench, last [xpainter-prof] line must
#      contain ab=0 and route=0 (merged stdout+stderr capture).
#   4. one machine-readable RESULT line + VERDICT=PASS/FAIL.
#      PASS when gain >= MinGainPct AND parity <= 90 AND ab=1.
# Exit codes: 0 pass, 1 gate fail, 2 missing baseline / environment error.
#
# NOTE: every executable is launched with its working directory set to
# bin-relwithdebinfo. Resources (fonts) are resolved relative to the exe
# directory; moving the exe elsewhere produces a false "slow" measurement.
# This file must stay ASCII-only (it runs under Windows PowerShell 5.1).

param(
    [switch]$Freeze,
    [string]$BaseExe = 'bin-relwithdebinfo\XGuiWindowDemo_GLbase.exe',
    [int]$Rounds = 3,
    [double]$MinGainPct = 1.5
)

$Repo = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$BinDir = Join-Path $Repo 'bin-relwithdebinfo'
$TestExe = Join-Path $BinDir 'XGuiWindowDemo_Test.exe'
$FreezeTarget = Join-Path $BinDir 'XGuiWindowDemo_GLbase.exe'
$OriginTarget = Join-Path $BinDir 'XGuiWindowDemo_GLorigin.exe'
$ParityExe = Join-Path $BinDir 'XGuiGpuParity_Test.exe'

function Format-Invariant {
    param([string]$Format, [double]$Value)
    return [string]::Format([System.Globalization.CultureInfo]::InvariantCulture,
                            $Format, $Value)
}

# Run one executable, capture stdout+stderr into a single string.
# Returns { Text, ExitCode }. ExitCode -1 means the process failed to start.
function Invoke-Capture {
    param(
        [Parameter(Mandatory = $true)][string]$Exe,
        [Parameter(Mandatory = $true)][string]$WorkDir,
        [string[]]$Arguments = @()
    )
    $stamp = [guid]::NewGuid().ToString('N')
    $outFile = Join-Path $env:TEMP ('gl_gate_' + $stamp + '.out')
    $errFile = Join-Path $env:TEMP ('gl_gate_' + $stamp + '.err')
    $proc = $null
    try {
        $proc = Start-Process -FilePath $Exe -ArgumentList $Arguments `
                    -WorkingDirectory $WorkDir -Wait -PassThru -NoNewWindow `
                    -RedirectStandardOutput $outFile -RedirectStandardError $errFile
    }
    catch {
        Remove-Item -LiteralPath $outFile, $errFile -Force -ErrorAction SilentlyContinue
        return [pscustomobject]@{ Text = ''; ExitCode = -1 }
    }
    $outText = ''
    $errText = ''
    if (Test-Path -LiteralPath $outFile) {
        $outText = [string](Get-Content -LiteralPath $outFile -Raw -Encoding Default)
    }
    if (Test-Path -LiteralPath $errFile) {
        $errText = [string](Get-Content -LiteralPath $errFile -Raw -Encoding Default)
    }
    Remove-Item -LiteralPath $outFile, $errFile -Force -ErrorAction SilentlyContinue
    return [pscustomobject]@{ Text = ($outText + "`n" + $errText); ExitCode = $proc.ExitCode }
}

function Get-Median {
    param([double[]]$Values)
    if ($null -eq $Values -or $Values.Count -eq 0) { return 0.0 }
    $sorted = @($Values | Sort-Object)
    $n = $sorted.Count
    if (($n % 2) -eq 1) {
        return [double]$sorted[[int](($n - 1) / 2)]
    }
    $mid = [int]($n / 2)
    return ([double]$sorted[$mid - 1] + [double]$sorted[$mid]) / 2.0
}

# One benchmark run: returns { Fps, Cpu, ExitCode } (Cpu/Fps are $null when
# the expected metric line is absent).
function Measure-Bench {
    param([string]$Exe, [int]$Seconds)
    $run = Invoke-Capture -Exe $Exe -WorkDir $BinDir `
               -Arguments @('--benchmark', [string]$Seconds, '--benchmark-full',
                            '--page', '4', '--tab', '20')
    $fps = $null
    $cpu = $null
    $m = [regex]::Matches($run.Text, 'fps=([0-9.]+)')
    if ($m.Count -gt 0) {
        $fps = [double]::Parse($m[$m.Count - 1].Groups[1].Value,
                               [System.Globalization.CultureInfo]::InvariantCulture)
    }
    $m = [regex]::Matches($run.Text, 'cpu=([0-9.]+)ms/frame')
    if ($m.Count -gt 0) {
        $cpu = [double]::Parse($m[$m.Count - 1].Groups[1].Value,
                               [System.Globalization.CultureInfo]::InvariantCulture)
    }
    return [pscustomobject]@{ Fps = $fps; Cpu = $cpu; ExitCode = $run.ExitCode }
}

# ---------------- 1. freeze mode ----------------
if ($Freeze) {
    if (-not (Test-Path -LiteralPath $TestExe)) {
        Write-Output ('FROZEN_FAIL missing ' + $TestExe)
        exit 2
    }
    Copy-Item -LiteralPath $TestExe -Destination $FreezeTarget -Force
    if (-not (Test-Path -LiteralPath $OriginTarget)) {
        Copy-Item -LiteralPath $TestExe -Destination $OriginTarget -Force
    }
    Write-Output 'FROZEN'
    exit 0
}

# ---------------- 2. resolve executables ----------------
if (-not [System.IO.Path]::IsPathRooted($BaseExe)) {
    $BaseExe = Join-Path $Repo $BaseExe
}
if (-not (Test-Path -LiteralPath $BaseExe)) {
    Write-Output ('NO_BASELINE ' + $BaseExe)
    exit 2
}
if (-not (Test-Path -LiteralPath $TestExe)) {
    Write-Output ('NO_CANDIDATE ' + $TestExe)
    exit 2
}
if ($Rounds -lt 1) { $Rounds = 1 }

Write-Output ('[gl-gate] base=' + $BaseExe)
Write-Output ('[gl-gate] cand=' + $TestExe)
Write-Output ('[gl-gate] rounds=' + $Rounds + ' minGainPct=' +
              (Format-Invariant '{0}' $MinGainPct))

# ---------------- 3. performance A/B ----------------
$env:XGUI_RENDER_BACKEND = 'gpu'
$env:XGUI_BENCH_CPU = '1'

$measureOk = $true
$baseCpuVals = @()
$candCpuVals = @()
for ($i = 1; $i -le $Rounds; $i++) {
    $rb = Measure-Bench -Exe $BaseExe -Seconds 5
    $rc = Measure-Bench -Exe $TestExe -Seconds 5
    if ($null -eq $rb.Cpu -or $null -eq $rc.Cpu) {
        $measureOk = $false
        Write-Output ('BENCH_NO_METRIC round=' + $i +
                      ' baseExit=' + $rb.ExitCode + ' candExit=' + $rc.ExitCode)
        continue
    }
    $baseCpuVals += $rb.Cpu
    $candCpuVals += $rc.Cpu
    $baseFps = 'n/a'
    if ($null -ne $rb.Fps) { $baseFps = Format-Invariant '{0:F1}' $rb.Fps }
    $candFps = 'n/a'
    if ($null -ne $rc.Fps) { $candFps = Format-Invariant '{0:F1}' $rc.Fps }
    Write-Output ('[gl-gate] round=' + $i +
                  ' base_cpu=' + (Format-Invariant '{0:F3}' $rb.Cpu) +
                  ' cand_cpu=' + (Format-Invariant '{0:F3}' $rc.Cpu) +
                  ' base_fps=' + $baseFps + ' cand_fps=' + $candFps)
}

$baseMed = Get-Median -Values $baseCpuVals
$candMed = Get-Median -Values $candCpuVals
if ($baseCpuVals.Count -eq 0) { $measureOk = $false }
if ($candCpuVals.Count -eq 0) { $measureOk = $false }

$gain = 0.0
if ($baseMed -gt 0.0) {
    $gain = ($baseMed - $candMed) / $baseMed * 100.0
}
$gainRounded = [math]::Round($gain, 2)

# ---------------- 4. parity (SW vs GL on the offscreen scene) ----------------
$outDir = Join-Path $Repo 'out'
if (-not (Test-Path -LiteralPath $outDir)) {
    New-Item -ItemType Directory -Force -Path $outDir | Out-Null
}
$swRaw = Join-Path $outDir 'gl_gate_sw.raw'
$glRaw = Join-Path $outDir 'gl_gate_gl.raw'
Remove-Item -LiteralPath $swRaw, $glRaw -Force -ErrorAction SilentlyContinue

$parityHard = 999999
$parityOk = $false
if (-not (Test-Path -LiteralPath $ParityExe)) {
    Write-Output ('PARITY_EXE_MISSING ' + $ParityExe)
}
else {
    $env:XGUI_RENDER_BACKEND = 'software'
    $null = Invoke-Capture -Exe $ParityExe -WorkDir $BinDir -Arguments @('--dump', $swRaw)
    $env:XGUI_RENDER_BACKEND = 'gpu'
    $null = Invoke-Capture -Exe $ParityExe -WorkDir $BinDir -Arguments @('--dump', $glRaw)
    $haveSw = (Test-Path -LiteralPath $swRaw) -and ((Get-Item -LiteralPath $swRaw).Length -gt 0)
    $haveGl = (Test-Path -LiteralPath $glRaw) -and ((Get-Item -LiteralPath $glRaw).Length -gt 0)
    if (-not ($haveSw -and $haveGl)) {
        Write-Output ('PARITY_DUMP_MISSING sw=' + $haveSw + ' gl=' + $haveGl)
    }
    else {
        $diff = Invoke-Capture -Exe $ParityExe -WorkDir $BinDir `
                    -Arguments @('--diff', $swRaw, $glRaw)
        $m = [regex]::Matches($diff.Text, 'hard\(>3/chan\) px=([0-9]+)')
        if ($m.Count -gt 0) {
            $parityHard = [int]$m[$m.Count - 1].Groups[1].Value
            $parityOk = $true
        }
        else {
            Write-Output ('PARITY_NO_MATCH exit=' + $diff.ExitCode)
        }
    }
}

# ---------------- 5. full-GPU check (ab=0 and route=0 on last prof line) ----------------
$env:XGUI_RENDER_BACKEND = 'gpu'
$env:XGPU_PAINTER_PROF = '1'
$prof = Invoke-Capture -Exe $TestExe -WorkDir $BinDir `
            -Arguments @('--benchmark', '3', '--benchmark-full',
                         '--page', '4', '--tab', '20')
$profLine = ''
if ($prof.Text) {
    $lines = @($prof.Text -split "`r?`n")
    for ($i = $lines.Count - 1; $i -ge 0; $i--) {
        if ($lines[$i] -match '\[xpainter-prof\]') {
            $profLine = $lines[$i]
            break
        }
    }
}
$abOk = 0
if ($profLine -ne '') {
    if (($profLine -match '(^|\s)ab=0(\s|$)') -and
        ($profLine -match '(^|\s)route=0(\s|$)')) {
        $abOk = 1
    }
    else {
        Write-Output ('PROF_LINE ' + $profLine)
    }
}
else {
    Write-Output ('PROF_LINE_MISSING exit=' + $prof.ExitCode)
}

Remove-Item Env:\XGPU_PAINTER_PROF -ErrorAction SilentlyContinue
Remove-Item Env:\XGUI_BENCH_CPU -ErrorAction SilentlyContinue
Remove-Item Env:\XGUI_RENDER_BACKEND -ErrorAction SilentlyContinue

# ---------------- 6. verdict ----------------
$pass = $measureOk -and $parityOk -and
        ($gainRounded -ge $MinGainPct) -and
        ($parityHard -le 90) -and
        ($abOk -eq 1)

Write-Output ('RESULT base=' + (Format-Invariant '{0:F3}' $baseMed) +
              ' cand=' + (Format-Invariant '{0:F3}' $candMed) +
              ' gain=' + (Format-Invariant '{0:F2}' $gainRounded) +
              ' parity=' + $parityHard +
              ' ab=' + $abOk)
if ($pass) {
    Write-Output 'VERDICT=PASS'
    exit 0
}
Write-Output 'VERDICT=FAIL'
exit 1
