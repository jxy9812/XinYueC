@echo off
REM ===========================================================================
REM  High-speed filter server, Python edition - SIMULATION MODE launcher
REM  (Windows desktop launcher, double-click)
REM
REM  Equivalent to:  python -m hsfsrv --sim --port 6200
REM
REM  What --sim means
REM  -----------------
REM  Only the device PHYSICAL layer is replaced (memory register models
REM  instead of the real serial port / TCP devices). devices.py and app.py
REM  run FOR REAL: engineering-unit conversion, status-word decoding, control
REM  interlocks, jog watchdog, per-valve 20s timers, alarm de-duplication,
REM  health push -- all genuine implementations. No serial port is opened and
REM  no TCP device is contacted.
REM
REM  Because of that, the real PLC / VFD / MT3A modules are NOT touched:
REM  this launcher is safe to double-click with hardware wired up.
REM
REM  Default behaviour of the simulated devices
REM  ------------------------------------------
REM    (2026-11-10: aligned with the 2026-11-09 ttyS4 field measurement
REM     doc -- station ids and per-meter byte order are config-driven,
REM     not hardcoded)
REM    valves   : 10s full travel (matches the field), 20s timeout threshold
REM    pump     : DO6, output only, no run feedback
REM    turbidity: stations 1 and 2, ~2.29 NTU (CDAB) / ~0.045 NTU + 19.3 C
REM               (DCBA, temperature kept device-side only)
REM    flowmeter: 3.6 m3/h +/-2%, empty-pipe alarm off, unit code 5 = m3/h
REM
REM  Useful overrides (edit the CMD line below, or pass on the command line):
REM    --sim-valve-move 25    valve travel 25s > 20s threshold -> alarm fires
REM    --sim-valve-move -1    valve stuck, never arrives
REM    --sim-valve-no-feedback   limit switches not wired (DI stays 0)
REM    --port 6201            run alongside the real server on 6200
REM
REM  Port note: the Python server defaults to 6200; the C edition uses 6100.
REM  Keeping them apart lets both servers run on one machine for comparison.
REM
REM  Keep this file pure ASCII: cmd.exe reads .bat with the OEM code page,
REM  so non-ASCII comments turn into garbage or bogus commands.
REM ===========================================================================
setlocal
cd /d "%~dp0"

REM UTF-8 console so the Chinese log lines survive redirection as well.
chcp 65001 >nul 2>nul
set "PYTHONIOENCODING=utf-8"

REM Pick an interpreter that actually runs. A bare `where py` is not enough:
REM the py launcher can exist without any Python 3 registered.
set "PYEXE="
py -3 -c "import sys" >nul 2>nul
if not errorlevel 1 set "PYEXE=py -3"
if not defined PYEXE (
    python -c "import sys" >nul 2>nul
    if not errorlevel 1 set "PYEXE=python"
)
if not defined PYEXE goto :nopython

echo ============================================================
echo  HSF server  -  SIMULATION MODE  (--sim)
echo  Devices are virtual; business logic is real.
echo  No serial port / no TCP device is opened.
echo ============================================================
echo  Working dir : %CD%
echo  Default port: 6200 (C edition uses 6100)
echo  Stop with   : Ctrl+C
echo.
echo  Simulated: valves 10s travel / pump DO6 / turbidity st.1+2
echo  Override :  run_server_sim.bat --sim-valve-move 25   (alarm test)
echo             run_server_sim.bat --sim-valve-move -1   (stuck valve)
echo             run_server_sim.bat --port 6201
echo.

%PYEXE% -m hsfsrv --sim --port 6200 %*
set "RC=%ERRORLEVEL%"

echo.
if "%RC%"=="0" goto :stopped
echo [EXIT] server returned code %RC%
goto :pause
:stopped
echo [EXIT] server stopped.
goto :pause

:nopython
echo [ERROR] No working Python 3 found in PATH.
echo         Install Python 3.9 or newer and make sure "python" or "py" works.

:pause
echo.
echo Press any key to close this window.
pause >nul
endlocal
