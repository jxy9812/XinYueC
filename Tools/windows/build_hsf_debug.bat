@echo off
rem HSF debug build: HsfClient + HsfServer + regression host + GUI probe + framework gates (bin\).
rem ASCII only. vcvars64 probed via vswhere (never hardcode the VS path).
set VSWHERE=C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set VSROOT=%%i
if "%VSROOT%"=="" echo VS_NOT_FOUND & exit /b 9
call "%VSROOT%\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
cmake -S F:\Code\CMake\XinYueC -B F:\Code\CMake\XinYueC\out\build\x64-Debug -G Ninja >nul 2>&1
ninja -C F:\Code\CMake\XinYueC\out\build\x64-Debug HsfClient HsfServer XinYueC_Static HsfVfdProbe XGuiRegression_Test XHsfCodec_Test
echo NINJA_EXIT=%ERRORLEVEL%
exit /b %ERRORLEVEL%
