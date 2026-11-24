@echo off
rem p1_gate.bat -- P1 stage gate for both configs (debug + release).
rem
rem Runs Tools/windows/gpu_stage_verify.sh p1 twice through Git Bash:
rem   - debug   : out/p1_dbg_gate.log
rem   - release : out/p1_rel_gate.log   (extra --release argument)
rem Exit codes / markers:
rem   0 + P1_BOTH_PASS    both configs green
rem   1 + P1_DEBUG_FAIL   debug gate failed
rem   1 + P1_RELEASE_FAIL release gate failed
rem   2 + BASH_NOT_FOUND no usable Git Bash found
rem Keep this file ASCII-only with CRLF line endings: cmd parses batch files
rem in the OEM codepage and non-ASCII / LF-only content breaks line splitting.
setlocal

rem Repo root = two levels up from this script (Tools\windows\..\..).
cd /d "%~dp0..\.." || exit /b 2
if not exist out mkdir out

rem Locate Git Bash. Detection order (2026-11-24: the old PATH-only probe was
rem not enough -- the Git for Windows installer only adds <Git>\cmd to PATH,
rem which has git.exe but NO bash.exe, so a plain Windows session (the gate
rem runner, unlike an interactive Git Bash) found only WSL/WindowsApps bash):
rem   1) "where bash" -- skip WSL (System32) and the WindowsApps shim: both
rem      cannot resolve /f/... POSIX paths.
rem   2) derive from git.exe on PATH: <dir>\..\bin\bash.exe (the usual
rem      <Git>\cmd\git.exe -> <Git>\bin\bash.exe), then <dir>\..\usr\bin,
rem      then <dir>\bash.exe for a bare <Git>\bin\git.exe.
rem   3) GitForWindows registry InstallPath (HKLM).
rem   4) common install dirs (Program Files, Program Files (x86), per-user).
set "BASH="
for /f "delims=" %%I in ('where bash 2^>nul') do (
  if not defined BASH echo %%I | findstr /i /c:"\System32\bash.exe" /c:"\WindowsApps\bash.exe" >nul || set "BASH=%%I"
)
for /f "delims=" %%I in ('where git 2^>nul') do (
  if not defined BASH if exist "%%~dpI..\bin\bash.exe" set "BASH=%%~dpI..\bin\bash.exe"
  if not defined BASH if exist "%%~dpI..\usr\bin\bash.exe" set "BASH=%%~dpI..\usr\bin\bash.exe"
  if not defined BASH if exist "%%~dpIbash.exe" set "BASH=%%~dpIbash.exe"
)
for /f "tokens=2,*" %%A in ('reg query "HKLM\SOFTWARE\GitForWindows" /v InstallPath 2^>nul ^| findstr /i "InstallPath"') do (
  if not defined BASH if exist "%%B\bin\bash.exe" set "BASH=%%B\bin\bash.exe"
  if not defined BASH if exist "%%B\usr\bin\bash.exe" set "BASH=%%B\usr\bin\bash.exe"
)
if not defined BASH if exist "%ProgramFiles%\Git\bin\bash.exe" set "BASH=%ProgramFiles%\Git\bin\bash.exe"
if not defined BASH if exist "%ProgramFiles(x86)%\Git\bin\bash.exe" set "BASH=%ProgramFiles(x86)%\Git\bin\bash.exe"
if not defined BASH if exist "%LocalAppData%\Programs\Git\bin\bash.exe" set "BASH=%LocalAppData%\Programs\Git\bin\bash.exe"
if not defined BASH if exist "C:\Program Files\Git\bin\bash.exe" set "BASH=C:\Program Files\Git\bin\bash.exe"
if not defined BASH (
  echo BASH_NOT_FOUND
  exit /b 2
)
echo [p1-gate] bash: %BASH%

"%BASH%" -lc "cd /f/Code/CMake/XinYueC && sh Tools/windows/gpu_stage_verify.sh p1" > out\p1_dbg_gate.log 2>&1
if errorlevel 1 (
  echo P1_DEBUG_FAIL
  exit /b 1
)

"%BASH%" -lc "cd /f/Code/CMake/XinYueC && sh Tools/windows/gpu_stage_verify.sh p1 --release" > out\p1_rel_gate.log 2>&1
if errorlevel 1 (
  echo P1_RELEASE_FAIL
  exit /b 1
)

echo P1_BOTH_PASS
exit /b 0
