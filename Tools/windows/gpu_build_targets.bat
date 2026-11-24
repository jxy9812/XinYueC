@echo off
rem GPU refactor: build acceptance targets.
rem usage: gpu_build_targets.bat [--release|--rwdi] [target ...]
rem   default: demo + smoke + parity test (Debug, out/build/x64-Debug -> bin/).
rem   --release: out/build/x64-Release -> bin-release/
rem   --rwdi   : out/build/x64-RelWithDebInfo -> bin-relwithdebinfo/
rem              (/O2 + /Zi: Release 速度 + 可采样符号，性能归因用)
rem MSVC environment must be loaded first (vcvars64); a bare cmake --build fails
rem with missing stdbool.h/stdint.h (C1083).
rem vcvars64.bat detection order (2026-11-23):
rem   1) VCVARS env var (explicit override)
rem   2) vswhere.exe (finds non-default install roots, incl. Preview/BuildTools)
rem   3) common install dirs under Program Files / Program Files (x86)
rem   4) vcvars64.bat on PATH
rem NOTE: keep this file ASCII-only: cmd parses the batch in the OEM codepage,
rem and UTF-8 comments can split lines and execute fragments.
setlocal EnableDelayedExpansion
rem NOTE: capture script dir BEFORE any shift; shift rewrites %0 and %~dp0
rem would then resolve against the (possibly relative) argument.
set "SCRIPT_DIR=%~dp0"
set "CONFIG=x64-Debug"
set "CONFIG_TYPE=Debug"
set "OUTDIR="
set "TARGETS="
:collect_args
if "%~1"=="" goto args_done
if /i "%~1"=="--release" (
  set "CONFIG=x64-Release"
  set "CONFIG_TYPE=Release"
  set "OUTDIR=%SCRIPT_DIR%..\..\bin-release"
) else if /i "%~1"=="--rwdi" (
  set "CONFIG=x64-RelWithDebInfo"
  set "CONFIG_TYPE=RelWithDebInfo"
  set "OUTDIR=%SCRIPT_DIR%..\..\bin-relwithdebinfo"
) else (
  set "TARGETS=!TARGETS! %~1"
)
shift
goto collect_args
:args_done
if "%TARGETS%"=="" set "TARGETS=XGuiWindowDemo_Test XGuiGpu_Test XGuiGpuParity_Test"
set "VCVARS="

if defined VCVARS if exist "%VCVARS%" goto have_vcvars

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" set "VSWHERE=%ProgramFiles%\Microsoft Visual Studio\Installer\vswhere.exe"
if exist "%VSWHERE%" (
  for /f "usebackq delims=" %%I in (`"%VSWHERE%" -latest -prerelease -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do (
    if exist "%%I\VC\Auxiliary\Build\vcvars64.bat" set "VCVARS=%%I\VC\Auxiliary\Build\vcvars64.bat"
  )
)
if defined VCVARS goto have_vcvars

for %%R in ("%ProgramFiles%" "%ProgramFiles(x86)%") do (
  for %%Y in (2026 18 2022 2019 2017) do (
    for %%E in (Enterprise Professional Community BuildTools Preview) do (
      if not defined VCVARS if exist "%%~R\Microsoft Visual Studio\%%Y\%%E\VC\Auxiliary\Build\vcvars64.bat" set "VCVARS=%%~R\Microsoft Visual Studio\%%Y\%%E\VC\Auxiliary\Build\vcvars64.bat"
    )
  )
)
if defined VCVARS goto have_vcvars

for /f "delims=" %%I in ('where vcvars64.bat 2^>nul') do if not defined VCVARS set "VCVARS=%%I"
if defined VCVARS goto have_vcvars

echo [gpu-build] FATAL: vcvars64.bat not found.
echo [gpu-build]   set VCVARS=C:\path\to\VC\Auxiliary\Build\vcvars64.bat and retry
exit /b 2

:have_vcvars
echo [gpu-build] vcvars: %VCVARS%
call "%VCVARS%" >nul 2>&1
if errorlevel 1 (
  echo [gpu-build] FATAL: vcvars64 failed
  exit /b 2
)
cd /d "%SCRIPT_DIR%..\.." || exit /b 2

if exist out\build\%CONFIG%\CMakeCache.txt goto build
echo [gpu-build] configure: %CONFIG% (%CONFIG_TYPE%)
if "%CONFIG%"=="x64-Debug" goto configure_debug
cmake -S . -B out\build\%CONFIG% -G Ninja -DCMAKE_BUILD_TYPE=%CONFIG_TYPE% -DCMAKE_RUNTIME_OUTPUT_DIRECTORY="%OUTDIR%" -DCMAKE_MAKE_PROGRAM="%VSINSTALLDIR%Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe"
if errorlevel 1 (echo [gpu-build] CONFIGURE_FAILED & exit /b 2)
goto build
:configure_debug
cmake -S . -B out\build\%CONFIG% -G Ninja -DCMAKE_BUILD_TYPE=%CONFIG_TYPE% -DCMAKE_MAKE_PROGRAM="%VSINSTALLDIR%Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe"
if errorlevel 1 (echo [gpu-build] CONFIGURE_FAILED & exit /b 2)
goto build

:build
set "ARGS="
for %%T in (%TARGETS%) do set "ARGS=!ARGS! --target %%T"
echo [gpu-build] build: %CONFIG% %TARGETS%
cmake --build out/build/%CONFIG%%ARGS%
if errorlevel 1 (
  echo [gpu-build] BUILD_FAILED
  exit /b 1
)
echo [gpu-build] BUILD_OK
exit /b 0
