@echo off
rem ===========================================================================
rem  FULL rebuild of the C test host (XinYueC_Static).
rem
rem    verify\rebuild_full.bat
rem
rem  [Why] This repo's ninja has NO header dependency tracking, so changing a
rem  conditional macro in a public header (XNETWORK_WIN_BACKEND lives in
rem  Src\XCore\XCode\XNetwork\XNetwork_config.h) leaves stale objects built
rem  under the old branch.  Mixing two layouts is a crash factory.  So: when
rem  the backend macro flips, wipe every .obj and rebuild from scratch.
rem
rem  [Rules]
rem  - Every MSVC build must go through vcvars64.bat.
rem  - PURE ASCII ONLY: cmd reads .bat in OEM code page.
rem  - CRLF line endings required by cmd.
rem ===========================================================================
setlocal enabledelayedexpansion
set ROOT=%~dp0..
cd /d "%ROOT%"

set VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe
set VSPATH=
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set VSPATH=%%i
if "%VSPATH%"=="" (
  echo [rebuild] vswhere found no VS installation
  exit /b 1
)
call "%VSPATH%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 (
  echo [rebuild] vcvars64 failed
  exit /b 1
)
echo [rebuild] deleting stale objects under out\build ...
del /s /q out\build\*.obj >nul 2>&1
cmake -S "%ROOT%" -B "%ROOT%\out\build\x64-Debug" >nul
if errorlevel 1 (
  echo [rebuild] cmake configure failed
  exit /b 1
)
echo [rebuild] building XinYueC_Static (full) ...
ninja -C "%ROOT%\out\build\x64-Debug" XinYueC_Static
set RC=%errorlevel%
echo [rebuild] ninja exit=%RC%
exit /b %RC%
