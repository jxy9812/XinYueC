@echo off
rem ASCII-only quiet wrapper for workflow build gate (stdout kept under 256KB).
call "%~dp0night_build_x64.bat" > "%TEMP%\qbuild_x64.log" 2>&1
set QEXIT=%ERRORLEVEL%
echo BUILD_EXIT=%QEXIT%
powershell -NoProfile -Command "Get-Content $env:TEMP\qbuild_x64.log -Tail 25"
exit /b %QEXIT%
