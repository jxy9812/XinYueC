@echo off
REM Night build script: x64-Debug tree (VS env + bundled ninja; cmake optional)
REM Usage: night_build_x64.bat [target]   (no target = build all)
REM
REM ASCII-only on purpose: cmd.exe reads .bat in the OEM codepage (GBK on a
REM Chinese Windows), so UTF-8 Chinese text in a .bat turns into mojibake and
REM the stray bytes get parsed as commands (breaks the script). Keep this file
REM ASCII. See quiet_build_x64.bat for the same convention.
REM
REM Visual Studio discovery: prefer vswhere (ships with the VS Installer, its
REM path is stable across machines). Fall back to the legacy hardcoded path
REM only when vswhere is absent. The hardcoded path dies silently whenever VS
REM moves to another drive or another version -- 2026-11-01: script still
REM pointed at C:\...\VS\18\Enterprise while the box actually had
REM D:\...\VS\2022\Preview, so the whole nightly build failed with
REM "The system cannot find the path specified".
setlocal EnableDelayedExpansion

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
set "VSROOT="
if exist "%VSWHERE%" (
    for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSROOT=%%i"
)

set "VCVARS="
set "NINJA="
if defined VSROOT (
    if exist "%VSROOT%\VC\Auxiliary\Build\vcvars64.bat" set "VCVARS=%VSROOT%\VC\Auxiliary\Build\vcvars64.bat"
    if exist "%VSROOT%\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe" set "NINJA=%VSROOT%\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe"
)
if not defined VCVARS if exist "C:\Program Files\Microsoft Visual Studio\18\Enterprise\VC\Auxiliary\Build\vcvars64.bat" set "VCVARS=C:\Program Files\Microsoft Visual Studio\18\Enterprise\VC\Auxiliary\Build\vcvars64.bat"
if not defined NINJA if exist "C:\Program Files\Microsoft Visual Studio\18\Enterprise\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe" set "NINJA=C:\Program Files\Microsoft Visual Studio\18\Enterprise\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe"

if not defined VCVARS (
    echo [night_build_x64] ERROR: vcvars64.bat not found - vswhere and fallback both missed
    exit /b 2
)
if not defined NINJA (
    echo [night_build_x64] ERROR: ninja.exe not found - vswhere and fallback both missed
    exit /b 2
)

call "%VCVARS%" >nul 2>&1
cd /d "%~dp0..\..\out\build\x64-Debug"
"%NINJA%" %*
endlocal & exit /b %ERRORLEVEL%
