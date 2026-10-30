@echo off
REM ============================================================================
REM  x64 Debug build entry (Windows) - VS toolchain + bundled Ninja
REM ----------------------------------------------------------------------------
REM  Usage:  Tools\windows\build_x64.bat [ninja-target]
REM          (no target = build all)
REM
REM  Why this file exists:
REM    - Tools\windows\night_build_x64.bat is the "night build" entry and points
REM      at "C:\Program Files\Microsoft Visual Studio\18\Enterprise" (VS18).
REM      On machines whose VS lives elsewhere (e.g. VS 2022 Preview on D:),
REM      that script fails while the build is perfectly fine.
REM    - The working variant used to live only in out\ (gitignored) and was
REM      lost on every clean; it now lives here with VS auto-detection.
REM
REM  VS detection order (first hit wins):
REM    1) VS 2022 Preview  (D:\ or C:\Program Files)
REM    2) VS 2022 Enterprise/Professional/Community
REM    3) VS 18 (Enterprise)
REM
REM  Notes:
REM    - vcvarsall.bat is called directly: vcvars64.bat is a %* passthrough
REM      wrapper and would forward the target name as a 2nd arch argument.
REM    - Build tree: out\build\x64-Debug (VSInheritEnvironments.txt is written
REM      there by CMake for the VS environment).
REM ============================================================================
setlocal
set "BUILD_DIR=%~dp0..\..\out\build\x64-Debug"
set "VSROOT="
set "VSARCH=x64"

REM ---- 1) VS 2022 Preview ----
for %%D in ("D:\Program Files\Microsoft Visual Studio\2022\Preview" "C:\Program Files\Microsoft Visual Studio\2022\Preview") do (
    if not defined VSROOT if exist "%%~fD\VC\Auxiliary\Build\vcvarsall.bat" set "VSROOT=%%~fD"
)
REM ---- 2) VS 2022 (Enterprise / Professional / Community) ----
if not defined VSROOT (
    for %%E in (Enterprise Professional Community BuildTools) do (
        if not defined VSROOT if exist "C:\Program Files\Microsoft Visual Studio\2022\%%E\VC\Auxiliary\Build\vcvarsall.bat" (
            set "VSROOT=C:\Program Files\Microsoft Visual Studio\2022\%%E"
        )
    )
)
REM ---- 3) VS 18 ----
if not defined VSROOT if exist "C:\Program Files\Microsoft Visual Studio\18\Enterprise\VC\Auxiliary\Build\vcvarsall.bat" (
    set "VSROOT=C:\Program Files\Microsoft Visual Studio\18\Enterprise"
)

if not defined VSROOT (
    echo ERROR: no Visual Studio found ^(tried VS 2022 Preview / 2022 / 18^).
    echo        Set VSPATH manually, or edit this script.
    exit /b 1
)

echo Using VS: %VSROOT%
call "%VSROOT%\VC\Auxiliary\Build\vcvarsall.bat" %VSARCH% >nul 2>&1
if errorlevel 1 (
    echo ERROR: vcvarsall.bat failed for %VSROOT%
    exit /b 1
)

set "NINJA=%VSROOT%\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe"
if not exist "%NINJA%" (
    echo ERROR: ninja.exe not found under %VSROOT%
    exit /b 1
)
if not exist "%BUILD_DIR%" (
    echo ERROR: build tree missing: %BUILD_DIR%
    echo        Configure it first ^(cmake -S . -B out/build/x64-Debug -G Ninja^).
    exit /b 1
)

cd /d "%BUILD_DIR%"
"%NINJA%" %*
exit /b %ERRORLEVEL%
