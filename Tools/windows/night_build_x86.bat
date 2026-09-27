@echo off
REM Night build script: x86-Debug tree (VS18 env + bundled ninja)
REM Usage: night_build_x86.bat [target]   (no target = build all)
setlocal
set NINJA=C:\Program Files\Microsoft Visual Studio\18\Enterprise\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe
set VCVARS=C:\Program Files\Microsoft Visual Studio\18\Enterprise\VC\Auxiliary\Build\vcvarsall.bat
call "%VCVARS%" x86 >nul 2>&1
cd /d D:\code\CMake\Container\out\build\x86-Debug
if "%1"=="" (
    "%NINJA%"
) else (
    "%NINJA%" %1
)
endlocal & exit /b %ERRORLEVEL%
