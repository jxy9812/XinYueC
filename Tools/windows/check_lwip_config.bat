@echo off
REM 单文件语法检查：lwIP 后端网卡配置实现（XNETWORK_USE_LWIP 开启态）
REM include 路径 = Src 全目录递归（与主构建 FIND_INCLUDE_DIR 同口径）+ lwIP 三目录
setlocal enabledelayedexpansion
set VCVARS=C:\Program Files\Microsoft Visual Studio\18\Enterprise\VC\Auxiliary\Build\vcvars64.bat
call "%VCVARS%" >nul 2>&1
cd /d "%~dp0..\.."
set RESP=out\lwip_check.rsp
del "%RESP%" >nul 2>&1
echo /c /utf-8 /WX- /DXNETWORK_USE_LWIP> "%RESP%"
for /d /r Src %%D in (*) do echo /I"%%D">> "%RESP%"
echo /ISrc>> "%RESP%"
echo /ILibrary\lwip\include>> "%RESP%"
echo /ILibrary\lwip\platform>> "%RESP%"
echo /ILibrary\lwip>> "%RESP%"
echo /Foout\xdevice_lwip_check.obj>> "%RESP%"
echo Library\lwip\platform\XDeviceNetwork_lwip.c>> "%RESP%"
cl @%RESP%
endlocal & exit /b %ERRORLEVEL%
