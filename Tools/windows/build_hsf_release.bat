@echo off
rem HSF release build: side-by-side output in bin-release\ (never overwrites bin\).
rem ASCII only. First run configures (long); later runs are incremental.
set VSWHERE=C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set VSROOT=%%i
if "%VSROOT%"=="" echo VS_NOT_FOUND & exit /b 9
call "%VSROOT%\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
cmake -S F:\Code\CMake\XinYueC -B F:\Code\CMake\XinYueC\out\build\x64-Release -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_RUNTIME_OUTPUT_DIRECTORY=F:\Code\CMake\XinYueC\bin-release
ninja -C F:\Code\CMake\XinYueC\out\build\x64-Release HsfServer HsfClient XinYueC_Static
echo NINJA_EXIT=%ERRORLEVEL%
exit /b %ERRORLEVEL%
