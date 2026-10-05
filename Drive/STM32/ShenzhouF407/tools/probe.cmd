@echo off
rem ==== XGuiDemoF407 runtime probe (J-Link halt + read state) ====
set GDB="C:\Users\jxy\.eide\tools\gcc_arm\bin\arm-none-eabi-gdb.exe"
set SRV="C:\Users\jxy\.eide\tools\jlink\JLinkGDBServerCL.exe"

taskkill /IM JLinkGDBServerCL.exe /F >nul 2>&1
start "" /B %SRV% -device STM32F407ZG -if SWD -speed 1000 -port 2331 -singlerun >"%TEMP%\gdbserver_probe.log" 2>&1
ping -n 10 127.0.0.1 >nul
%GDB% -batch -ex "target remote localhost:2331" -ex "monitor halt" -ex "info registers pc" -ex "bt 8" -ex "print/x *(unsigned short *)&s_lcd_id" -ex "print/x *(unsigned int *)&pxCurrentTCB" -ex "print/x *(unsigned int*)0xE000ED28" -ex "print/x *(unsigned int*)0xE000ED2C" -ex "print/x *(unsigned int*)0xE000ED34" -ex "print/x *(unsigned int*)0xE000ED38" -ex "print/x *(void **)&xPortStrayFreePtr" -ex "print/x *(void **)&xPortStrayFreeCaller" -ex "print/x *(void **)&g_strayFreePtr" -ex "print/x *(void **)&g_strayFreeCaller" -ex "print/x *(unsigned int *)&xFreeBytesRemaining" -ex "x/8wx &g_sramDebug" -ex "print/x *(void **)&xStart.pxNextFreeBlock" -ex "print/x *(unsigned int *)&xStart.xBlockSize" -ex "print/x *(void **)&pxEnd" -ex "detach" bin\XGuiDemoF407.elf
set RC=%ERRORLEVEL%
taskkill /IM JLinkGDBServerCL.exe /F >nul 2>&1
exit /b %RC%
