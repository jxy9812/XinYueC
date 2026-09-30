@echo off
rem ASCII-only autotest gate. File-redirected: PS pipe deadlock trap (ucrtbased fwrite).
cd /d "%~dp0..\..\bin"
XGuiWindowDemo_Test.exe --autotest > "%TEMP%\qautotest.log" 2>&1
set AEXIT=%ERRORLEVEL%
echo AUTOTEST_EXIT=%AEXIT%
findstr /c:FAIL "%TEMP%\qautotest.log"
echo FAILSCAN_EXIT=%ERRORLEVEL%
powershell -NoProfile -Command "Get-Content $env:TEMP\qautotest.log -Tail 12"
exit /b %AEXIT%
