# Run one test suite under a wall-clock timeout and report its REAL exit code.
#
#   verify\run_one_suite.ps1 -Exe <exe> [-Arg1 <a>] [-Arg2 <b>] [-TimeoutMs <ms>]
#
# Exit codes:
#   0   - the suite succeeded
#   124 - the suite exceeded the timeout (was killed)
#   125 - the exit code could not be obtained (the caller must treat this as FAIL)
#
# [Why not Start-Process -PassThru]  A Process object returned by
# `Start-Process -PassThru` reports a NULL ExitCode unless the process handle
# was cached (`$p.Handle`) or `WaitForExit()` was called on that object.
# The symptom is nasty: every suite looked GREEN even when it failed
# (2026-11-21, three rounds of false green). Using the .NET Process API
# directly gives a reliable ExitCode.
#
# PURE ASCII ONLY (Windows PowerShell 5.1 reads .ps1 as ANSI without a BOM).
param(
    [Parameter(Mandatory = $true)][string]$Exe,
    [string]$Arg1 = "",
    [string]$Arg2 = "",
    [int]$TimeoutMs = 180000
)

$psi = New-Object System.Diagnostics.ProcessStartInfo
$psi.FileName = $Exe
$psi.Arguments = ("$Arg1 $Arg2").Trim()
$psi.UseShellExecute = $false

$p = [System.Diagnostics.Process]::Start($psi)
if ($null -eq $p) {
    Write-Host "[ERROR] failed to start $Exe"
    exit 125
}

if (-not $p.WaitForExit($TimeoutMs)) {
    try { $p.Kill() } catch {}
    Write-Host "[TIMEOUT] suite exceeded $TimeoutMs ms"
    exit 124
}

# Parameterless WaitForExit: makes sure the exit code is fully materialized.
$p.WaitForExit()
if ($null -eq $p.ExitCode) {
    Write-Host "[ERROR] ExitCode unavailable"
    exit 125
}
exit $p.ExitCode
