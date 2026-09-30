@echo off
REM Night build script: x64-Debug tree (VS18 env + bundled ninja; cmake optional)
REM Usage: night_build_x64.bat [target]   (no target = build all)
setlocal
set NINJA=C:\Program Files\Microsoft Visual Studio\18\Enterprise\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe
set VCVARS=C:\Program Files\Microsoft Visual Studio\18\Enterprise\VC\Auxiliary\Build\vcvars64.bat
call "%VCVARS%" >nul 2>&1
cd /d "%~dp0..\..\out\build\x64-Debug"
if "%1"=="" (
    "%NINJA%" %*
) else (
    "%NINJA%" %1
)
endlocal & exit /b %ERRORLEVEL%
