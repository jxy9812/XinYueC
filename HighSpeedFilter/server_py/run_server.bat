@echo off
REM ===========================================================================
REM  High-speed filter server, Python edition - desktop launcher (Windows)
REM
REM  Double-click this file to start the server.
REM
REM  It runs the package form `python -m hsfsrv`, NOT `hsfsrv\__main__.py`.
REM  Running __main__.py directly used to die instantly with
REM      ImportError: attempted relative import with no known parent package
REM  which looked like "console flashes and vanishes". The __main__ fallback
REM  now also handles that case, but the package form is the recommended one.
REM
REM  Arguments are passed through, for example:
REM      run_server.bat --no-serial
REM      run_server.bat --diag
REM      run_server.bat --port 6201
REM
REM  Keep this file pure ASCII: cmd.exe reads .bat with the OEM code page,
REM  so non-ASCII comments turn into garbage or bogus commands.
REM ===========================================================================
setlocal
cd /d "%~dp0"

REM UTF-8 console so Chinese log lines survive redirection as well.
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

echo Starting server, default port 6200, Ctrl+C to stop.
echo Working dir: %CD%
echo.
%PYEXE% -m hsfsrv %*
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
