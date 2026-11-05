@echo off
REM ============================================================================
REM  xlsx_bridge.dll 编译脚本
REM  目标定义在主构建：HighSpeedFilter/CMakeLists.txt 的 xlsx_bridge 目标
REM  （链接 XinYueCoreS + Library 外部库，与 XDataTest 同口径）。
REM  产出自动拷贝到 client_py/hsfsrv_client/bin/xlsx_bridge.dll。
REM
REM  用法：build.bat [主构建目录]（缺省 ..\..\..\..\build\w3）
REM ============================================================================
setlocal
set "BUILD_DIR=%~1"
if "%BUILD_DIR%"=="" set "BUILD_DIR=%~dp0..\..\..\..\build\w3"
if not exist "%BUILD_DIR%\CMakeCache.txt" (
    echo [ERROR] 主构建目录不存在：%BUILD_DIR%
    echo 先在仓库根执行一次完整 cmake 配置，或把已配置的构建目录当参数传入。
    exit /b 2
)
cmake --build "%BUILD_DIR%" --config Release --target xlsx_bridge
endlocal
