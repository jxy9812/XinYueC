@echo off
rem ===========================================================================
rem  Incremental build of the C test host (XinYueC_Static).
rem
rem    verify\build_static.bat
rem
rem  [Why this file exists] out\build\x64-Debug\zbuild.bat is the same job but
rem  it lives under out\ which is git-ignored, so a fresh clone does not have
rem  it.  This one is committed and self-contained.
rem
rem  [Rules]
rem  - Every MSVC build must go through vcvars64.bat; calling cmake/ninja
rem    from a bare Git Bash shell fails with C1083 (stdbool.h/stdint.h).
rem  - cmake is re-run on purpose: it is what picks up newly added .c files
rem    (the GLOB in the CMake lists).
rem  - PURE ASCII ONLY: cmd reads .bat in OEM code page; UTF-8 Chinese bytes
rem    become bogus commands.
rem  - CRLF line endings required by cmd.
rem ===========================================================================
setlocal enabledelayedexpansion
set ROOT=%~dp0..
cd /d "%ROOT%"

set VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe
set VSPATH=
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set VSPATH=%%i
if "%VSPATH%"=="" (
  echo [build] vswhere found no VS installation
  exit /b 1
)
call "%VSPATH%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 (
  echo [build] vcvars64 failed
  exit /b 1
)
cmake -S "%ROOT%" -B "%ROOT%\out\build\x64-Debug" >nul
if errorlevel 1 (
  echo [build] cmake configure failed
  exit /b 1
)
ninja -C "%ROOT%\out\build\x64-Debug" XinYueC_Static
set RC=%errorlevel%
echo [build] ninja exit=%RC%
exit /b %RC%
