@echo off
rem ASCII-only quiet wrapper for the XGuiRegression_Test gate (file-redirected).
cd /d "%~dp0..\..\bin"
XGuiRegression_Test.exe > "%TEMP%\qregression.log" 2>&1
set REXIT=%ERRORLEVEL%
echo REGRESSION_EXIT=%REXIT%
powershell -NoProfile -Command "Get-Content $env:TEMP\qregression.log -Tail 20"
exit /b %REXIT%
