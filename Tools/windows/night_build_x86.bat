@echo off
REM Night build script: x86-Debug tree (VS env + bundled ninja)
REM Usage: night_build_x86.bat [target]   (no target = build all)
REM
REM ASCII-only on purpose: cmd.exe reads .bat in the OEM codepage (GBK on a
REM Chinese Windows), so UTF-8 Chinese text in a .bat turns into mojibake and
REM the stray bytes get parsed as commands (breaks the script). Keep this file
REM ASCII. See night_build_x64.bat for the same convention.
REM
REM Visual Studio discovery: prefer vswhere (ships with the VS Installer, its
REM path is stable across machines). Fall back to the legacy hardcoded path
REM only when vswhere is absent. Same reason as night_build_x64.bat.
setlocal EnableDelayedExpansion

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
set "VSROOT="
if exist "%VSWHERE%" (
    for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSROOT=%%i"
)

set "VCVARS="
set "NINJA="
if defined VSROOT (
    if exist "%VSROOT%\VC\Auxiliary\Build\vcvarsall.bat" set "VCVARS=%VSROOT%\VC\Auxiliary\Build\vcvarsall.bat"
    if exist "%VSROOT%\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe" set "NINJA=%VSROOT%\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe"
)
if not defined VCVARS if exist "C:\Program Files\Microsoft Visual Studio\18\Enterprise\VC\Auxiliary\Build\vcvarsall.bat" set "VCVARS=C:\Program Files\Microsoft Visual Studio\18\Enterprise\VC\Auxiliary\Build\vcvarsall.bat"
if not defined NINJA if exist "C:\Program Files\Microsoft Visual Studio\18\Enterprise\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe" set "NINJA=C:\Program Files\Microsoft Visual Studio\18\Enterprise\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe"

if not defined VCVARS (
    echo [night_build_x86] ERROR: vcvarsall.bat not found - vswhere and fallback both missed
    exit /b 2
)
if not defined NINJA (
    echo [night_build_x86] ERROR: ninja.exe not found - vswhere and fallback both missed
    exit /b 2
)

call "%VCVARS%" x86 >nul 2>&1
cd /d "%~dp0..\..\out\build\x86-Debug"
"%NINJA%" %*
endlocal & exit /b %ERRORLEVEL%
