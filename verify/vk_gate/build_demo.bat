@echo off
setlocal
set ROOT=F:\Code\CMake\XinYueC
set VSWHERE="%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
for /f "usebackq tokens=*" %%i in (`%VSWHERE% -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set VSPATH=%%i
if "%VSPATH%"=="" (echo NO_VS & exit /b 1)
call "%VSPATH%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 (echo VCVARS_FAIL & exit /b 1)
taskkill /F /IM XGuiWindowDemo_Test.exe >nul 2>nul
for /r "%ROOT%\out\build\x64-Debug" %%f in (*.obj) do del /Q "%%f" >nul 2>nul
cmake -S "%ROOT%" -B "%ROOT%\out\build\x64-Debug" >nul
if errorlevel 1 (echo CMAKE_FAIL & exit /b 1)
ninja -C "%ROOT%\out\build\x64-Debug" XGuiWindowDemo_Test
set RC=%errorlevel%
echo [build] ninja exit=%RC%
exit /b %RC%
