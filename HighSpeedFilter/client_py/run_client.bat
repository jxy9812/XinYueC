@echo off
REM ===========================================================================
REM  High-speed filter client, Python edition - desktop launcher (Windows)
REM
REM  Double-click to start. Start the server first: on launch the client
REM  probes 127.0.0.1 on ports 6200 / 6100, and if something is listening it
REM  logs in as root automatically; otherwise it shows the login dialog.
REM
REM  Arguments are passed through, for example:
REM      run_client.bat --login
REM      run_client.bat --auto 192.168.0.10
REM      run_client.bat --offline
REM
REM  Keep this file pure ASCII: cmd.exe reads .bat with the OEM code page.
REM ===========================================================================
setlocal
cd /d "%~dp0"

chcp 65001 >nul 2>nul
set "PYTHONIOENCODING=utf-8"

set "PYEXE="
py -3 -c "import sys" >nul 2>nul
if not errorlevel 1 set "PYEXE=py -3"
if not defined PYEXE (
    python -c "import sys" >nul 2>nul
    if not errorlevel 1 set "PYEXE=python"
)
if not defined PYEXE goto :nopython

echo Starting client ...
echo Working dir: %CD%
echo.
%PYEXE% -m hsfsrv_client %*
set "RC=%ERRORLEVEL%"

if "%RC%"=="0" goto :end
echo.
echo [EXIT] client returned code %RC%
echo Press any key to close this window.
pause >nul
goto :end

:nopython
echo [ERROR] No working Python 3 found in PATH.
echo         Install Python 3.9 or newer and make sure "python" or "py" works.
echo Press any key to close this window.
pause >nul

:end
endlocal
