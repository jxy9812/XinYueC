@echo off
rem ===========================================================================
rem  Windows socket backend gates - the suites that cover the WSAEventSelect /
rem  IOCP socket backends and the layers above them.
rem
rem    verify\run_socket_gates.bat
rem
rem  [Suites]
rem    xsocket-stall            socket read-stall / backpressure / reconnect
rem    xsocket-pipe             named pipe (local stream) transport
rem    udp-directed            UDP directed send/receive
rem    udp-bind-family         UDP dual-stack bind
rem    xlink-client             XLink client state machine + reconnect
rem    xmqtt-tcp-api            XMqtt TCP server API
rem    xconsole-shell-backend   console shell SSH/Telnet backends
rem
rem  [Rules]
rem  - Each step has its own exit code; any non-zero fails the run.
rem    Compare the raw errorlevel as a STRING (a crashed child yields a
rem    negative code that `if errorlevel 1` would treat as green).
rem  - Each step runs under a **wall-clock timeout** (see :step): a hung suite
rem    must fail the gate, not hang the gate. Exit code 124 = timed out.
rem    (2026-11-21: the IOCP backend's pipe path hangs the pipe suite.)
rem  - **NEGATIVE CONTROL FIRST** (see :selftest): before the real suites, the
rem    gate runs a command that is GUARANTEED to fail and requires itself to
rem    report FAIL. If it cannot detect that, the gate is broken and the run
rem    aborts with exit code 3. (2026-11-21: a PowerShell `Start-Process
rem    -PassThru` object reports a NULL ExitCode unless `WaitForExit()` is
rem    called on it -> every suite looked green even when it failed.)
rem  - PURE ASCII ONLY: cmd reads .bat in OEM code page.
rem  - CRLF line endings required by cmd.
rem ===========================================================================
setlocal enabledelayedexpansion
set ROOT=%~dp0..
cd /d "%ROOT%"

set FAILED=0
set RAN=0
set FAILEDNAMES=
set TIMEOUT_MS=180000

echo ============================================================
echo  Windows socket backend gates
echo ============================================================

if not exist bin\XinYueC_Static.exe (
  echo [ERROR] bin\XinYueC_Static.exe not built - run verify\build_static.bat first
  exit /b 2
)

rem ------------------------------------------------------------------ freshness
rem  The binary must be newer than every source: otherwise we are testing an
rem  OLD build and the whole gate is a false green.
powershell -NoProfile -ExecutionPolicy Bypass -Command "$exe = Get-Item 'bin\XinYueC_Static.exe'; $files = @(Get-ChildItem -Path 'Src','Drive','Test' -Recurse -File -Include *.c,*.h) + @(Get-Item 'main.c'); $newest = $files | Sort-Object LastWriteTime -Descending | Select-Object -First 1; if ($newest -and ($newest.LastWriteTime -gt $exe.LastWriteTime)) { Write-Host ('[STALE] binary is older than: ' + $newest.FullName); exit 1 }; exit 0"
if errorlevel 1 (
  echo [ERROR] bin\XinYueC_Static.exe is OLDER than the sources - rebuild first
  exit /b 2
)
echo [OK] binary is newer than the sources

rem ------------------------------------------------------------------ selftest
rem  Negative control: a command that must fail. If the gate reports it as
rem  passing, the gate cannot detect failures -> abort (do not run the suites).
call :step "SELFCHECK-negative-control" bin\XinYueC_Static.exe --test __gate_selfcheck_must_fail__
if "%FAILED%"=="0" (
  echo.
  echo [FATAL] The gate can NOT detect a failing command ^(negative control passed^).
  echo         Every result below would be meaningless - aborting.
  exit /b 3
)
echo [OK] negative control behaved as expected ^(gate can detect failures^)
set FAILED=0
set RAN=0
set FAILEDNAMES=

call :step "xsocket-stall" bin\XinYueC_Static.exe --test xsocket-stall
call :step "xsocket-pipe" bin\XinYueC_Static.exe --test xsocket-pipe
call :step "udp-directed" bin\XinYueC_Static.exe --test udp-directed
call :step "udp-bind-family" bin\XinYueC_Static.exe --test udp-bind-family
call :step "xlink-client" bin\XinYueC_Static.exe --test xlink-client
call :step "xmqtt-tcp-api" bin\XinYueC_Static.exe --test xmqtt-tcp-api
call :step "xconsole-shell-backend" bin\XinYueC_Static.exe --test xconsole-shell-backend

echo.
echo ============================================================
if "%FAILED%"=="0" goto :all_green
echo  SOCKET GATES FAILED %FAILED% of %RAN% suites
echo  failing:!FAILEDNAMES!
echo ============================================================
exit /b 1

:all_green
echo  SOCKET GATES ALL GREEN - ran %RAN% suites
echo ============================================================
exit /b 0

rem ------------------------------------------------------------------ one step
rem  Run one suite under a wall-clock timeout. Exit code 124 = timed out.
:step
set /a RAN+=1
echo.
echo ---- [step %RAN%] %~1 ----
powershell -NoProfile -ExecutionPolicy Bypass -File verify\run_one_suite.ps1 -Exe "%~2" -Arg1 "%~3" -Arg2 "%~4" -TimeoutMs %TIMEOUT_MS%
set "STEPERR=!errorlevel!"
if "!STEPERR!"=="124" (
  echo [FAIL] %~1 timed out after %TIMEOUT_MS% ms
  set /a FAILED+=1
  set "FAILEDNAMES=!FAILEDNAMES! [%~1:timeout]"
) else if "!STEPERR!"=="125" (
  echo [FAIL] %~1 exit code unavailable ^(gate cannot judge it^)
  set /a FAILED+=1
  set "FAILEDNAMES=!FAILEDNAMES! [%~1:noexitcode]"
) else if not "!STEPERR!"=="0" (
  echo [FAIL] %~1 exit=!STEPERR!
  set /a FAILED+=1
  set "FAILEDNAMES=!FAILEDNAMES! [%~1]"
) else (
  echo [OK] %~1
)
goto :eof
