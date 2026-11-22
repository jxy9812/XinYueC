@echo off
rem ===========================================================================
rem  HSF three-end full regression - single round (C + PY client + leak + PY server)
rem
rem   verify\wf\full_cycle.bat <round-tag>
rem
rem  [Rules]
rem   - Each step has its own exit code; any non-zero fails the round.
rem     Compare raw errorlevel as STRING (a crashed child yields a negative
rem     code that `if errorlevel 1` would treat as green).
rem   - Every script self-starts/stops its own processes on free ports;
rem     this runner never touches the resident 6100/8080.
rem   - PURE ASCII ONLY: cmd reads .bat in OEM code page; UTF-8 Chinese
rem     bytes become bogus commands (AGENTS.md hard rule).
rem   - CRLF line endings required by cmd.
rem ===========================================================================
setlocal enabledelayedexpansion
set ROOT=%~dp0..\..
cd /d "%ROOT%"
set PYTHONIOENCODING=utf-8
set PY=D:\anaconda3\python.exe
if not exist "%PY%" set PY=python
set TAG=%~1
if "%TAG%"=="" set TAG=one
rem [ASCII-ized] rem [????] ?????????--with-c?? C ????/ --web?????????
set WITH_C=0
set WITH_WEB=0
for %%A in (%*) do (
  if "%%~A"=="--with-c" set WITH_C=1
  if "%%~A"=="--web" set WITH_WEB=1
)

set FAILED=0
set RAN=0
set FAILEDNAMES=

echo ============================================================
echo  HSF full cycle round=%TAG%
echo ============================================================

rem ---- 0) C total regression: OPT-IN only ----
rem      [2026-11-21 user scope: C side dropped for this round]
rem      C regression is OFF by default; pass --with-c to enable.
rem      (not part of this round's acceptance scope).
rem      [why it was flaky] S3/S13 are load-sensitive (waiting for device
rem      feedback depends on polling; CPU preemption shortens the window):
rem      it went red twice in 5 rounds, green twice when run alone (1099/0).
rem      The root fix is on the test side (wait for state, not for time).
if "%WITH_C%"=="1" (
  call :step "C total regression" bin\XinYueC_Static.exe --test hsf-regression
)

rem ---- 1) PY client total regression (self-starts private sim) ----
cd /d "%ROOT%\HighSpeedFilter\client_py"
call :step "PY client total" %PY% -u tests\test_hsf_regression.py
cd /d "%ROOT%"

rem ---- 2) PY client leak gate (J/K/L/M UI lifecycle) ----
cd /d "%ROOT%\HighSpeedFilter\client_py"
call :step "PY client leak gate" %PY% -I tests\test_leak_gate.py
cd /d "%ROOT%"

rem ---- 3) PY server suite (35 groups) ----
cd /d "%ROOT%\HighSpeedFilter\server_py"
call :step "PY server suite" %PY% -u tests\run_all.py
cd /d "%ROOT%"

rem ---- 4) web gates (opt-in: pass --web as 2nd arg) ----
rem      layout geometry (6 viewports) + leak/console gate + functional check.
rem      They are opt-in because they need Edge and take ~5 min; the main
rem      round stays Edge-free so it can run unattended.
if "%WITH_WEB%"=="1" (
  call :step "web layout geometry" %PY% -u verify\webui\webui_layout_check.py --own
  call :step "web leak + console" %PY% -u verify\webui\webui_leak_gate.py
  call :step "web functional" %PY% -u verify\webui\webui_check.py --server-port 6200
)

echo.
echo ============================================================
if "%FAILED%"=="0" goto :green
echo  ROUND %TAG% FAILED %FAILED% of %RAN%
echo  failing:!FAILEDNAMES!
echo ============================================================
exit /b 1
:green
echo  ROUND %TAG% ALL GREEN (%RAN% steps)
echo ============================================================
exit /b 0

:step
set /a RAN+=1
echo.
echo ---- [step %RAN%] %~1 ----
%2 %3 %4 %5 %6 %7 %8
set "ERR=!errorlevel!"
if not "!ERR!"=="0" (
  echo [FAIL] %~1 exit=!ERR!
  set /a FAILED+=1
  set "FAILEDNAMES=!FAILEDNAMES! [%~1]"
) else (
  echo [OK] %~1
)
goto :eof
