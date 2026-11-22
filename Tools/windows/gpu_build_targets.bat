@echo off
rem GPU refactor: build acceptance targets.
rem usage: gpu_build_targets.bat [target ...]
rem   default: demo + smoke + parity test.
rem MSVC environment must be loaded first (vcvars64); a bare cmake --build fails
rem with missing stdbool.h/stdint.h (C1083).
rem NOTE: keep this file ASCII-only: cmd parses the batch in the OEM codepage,
rem and UTF-8 comments can split lines and execute fragments.
setlocal EnableDelayedExpansion
set "VCVARS=C:\Program Files\Microsoft Visual Studio\18\Enterprise\VC\Auxiliary\Build\vcvars64.bat"
if not exist "%VCVARS%" (
  echo [gpu-build] FATAL: vcvars64.bat not found: %VCVARS%
  exit /b 2
)
call "%VCVARS%" >nul 2>&1
if errorlevel 1 (
  echo [gpu-build] FATAL: vcvars64 failed
  exit /b 2
)
cd /d "%~dp0\..\.." || exit /b 2

set "TARGETS=%*"
if "%TARGETS%"=="" set "TARGETS=XGuiWindowDemo_Test XGuiGpu_Test XGuiGpuParity_Test"

set "ARGS="
for %%T in (%TARGETS%) do set "ARGS=!ARGS! --target %%T"
echo [gpu-build] build: %TARGETS%
cmake --build out/build/x64-Debug%ARGS%
if errorlevel 1 (
  echo [gpu-build] BUILD_FAILED
  exit /b 1
)
echo [gpu-build] BUILD_OK
exit /b 0
