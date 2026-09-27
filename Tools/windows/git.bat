@echo off
REM Git wrapper: uses VS-bundled git (not on PATH on this machine)
setlocal
set GITEXE=C:\Program Files\Microsoft Visual Studio\18\Enterprise\Common7\IDE\CommonExtensions\Microsoft\TeamFoundation\Team Explorer\Git\cmd\git.exe
"%GITEXE%" %*
endlocal & exit /b %ERRORLEVEL%
