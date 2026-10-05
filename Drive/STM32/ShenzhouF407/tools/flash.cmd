@echo off
rem ==================== XGuiDemoF407 烧录脚本（J-Link + gdb） ====================
rem 用法：cmd /c flash.cmd   （在仓库根目录执行；依赖 bin/XGuiDemoF407.elf）
set GDB="C:\Users\jxy\.eide\tools\gcc_arm\bin\arm-none-eabi-gdb.exe"
set SRV="C:\Users\jxy\.eide\tools\jlink\JLinkGDBServerCL.exe"

taskkill /IM JLinkGDBServerCL.exe /F >nul 2>&1
start "" /B %SRV% -device STM32F407ZG -if SWD -speed 1000 -port 2331 -singlerun >"%TEMP%\gdbserver_flash.log" 2>&1
ping -n 10 127.0.0.1 >nul
%GDB% -batch -ex "set confirm off" -ex "target remote localhost:2331" -ex "monitor reset halt" -ex "load" -ex "monitor reset" -ex "monitor go" bin\XGuiDemoF407.elf
set RC=%ERRORLEVEL%
ping -n 7 127.0.0.1 >nul
taskkill /IM JLinkGDBServerCL.exe /F >nul 2>&1
exit /b %RC%
