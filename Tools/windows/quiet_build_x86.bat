@echo off
rem ASCII-only quiet wrapper for x86 build (workflow gate use).
call "%~dp0night_build_x86.bat" > "%TEMP%\qbuild_x86.log" 2>&1
set QEXIT=%ERRORLEVEL%
echo BUILD_EXIT=%QEXIT%
powershell -NoProfile -Command "Get-Content $env:TEMP\qbuild_x86.log -Tail 25"
exit /b %QEXIT%
