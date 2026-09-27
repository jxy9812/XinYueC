@echo off
REM Regenerate Vulkan loader import libraries for both architectures from
REM the system loader DLLs (no Vulkan SDK needed):
REM   x64 <- C:\Windows\System32\vulkan-1.dll   (64-bit loader)
REM   x86 <- C:\Windows\SysWOW64\vulkan-1.dll   (32-bit loader)
REM Final artifacts (committed to git, shared across same-platform hosts):
REM   Tools\windows\vulkan\importlib\x64-msvc\vulkan-1.lib
REM   Tools\windows\vulkan\importlib\x86-msvc\vulkan-1.lib
REM All intermediates stay in Tools\windows\vulkan\importlib\ (gitignored) and can
REM be deleted afterwards; only the two published .lib files matter.
REM
REM The x86 lib additionally needs a stdcall thunk layer
REM (_vkCreateInstance@12: jmp _vkCreateInstance) because vulkan.h declares
REM vk functions as __stdcall on 32-bit Windows. Thunk names come from real
REM compiler-decorated symbols via gen_vulkan_x86_def.ps1; lib /def aliasing
REM cannot express this (it records the entry name verbatim as the DLL
REM export name, which would break runtime binding).
setlocal
set MSVCBIN=C:\Program Files\Microsoft Visual Studio\18\Enterprise\VC\Tools\MSVC\14.51.36231\bin\Hostx64\x64
set VCVARS=C:\Program Files\Microsoft Visual Studio\18\Enterprise\VC\Auxiliary\Build\vcvarsall.bat
set OUTDIR=D:\code\CMake\Container\Tools\windows\vulkan\importlib
cd /d "%OUTDIR%" || exit /b 1

echo == x64: dumpbin System32 vulkan-1.dll ==
"%MSVCBIN%\dumpbin.exe" /exports C:\Windows\System32\vulkan-1.dll > dumpbin-x64.txt
call :make_def dumpbin-x64.txt vulkan-1.def
"%MSVCBIN%\lib.exe" /nologo /def:vulkan-1.def /machine:X64 /out:vulkan-1-x64.lib || exit /b 1

echo == x86: dumpbin SysWOW64 vulkan-1.dll ==
"%MSVCBIN%\dumpbin.exe" /exports C:\Windows\SysWOW64\vulkan-1.dll > dumpbin-x86.txt
call :make_def dumpbin-x86.txt vulkan-1-x86-plain.def
"%MSVCBIN%\lib.exe" /nologo /def:vulkan-1-x86-plain.def /machine:X86 /out:vulkan-1-x86-plain.lib || exit /b 1

echo == x86: stdcall thunk layer (ml.exe jmp stubs) ==
call "%VCVARS%" x86 >nul 2>&1
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0gen_vulkan_x86_def.ps1" || exit /b 1
"%MSVCBIN%\lib.exe" /nologo /out:vulkan-1-x86.lib vulkan-1-x86-plain.lib vk_x86_stubs.obj || exit /b 1

echo == publish to arch dirs ==
if not exist x64-msvc mkdir x64-msvc
if not exist x86-msvc mkdir x86-msvc
copy /y vulkan-1-x64.lib x64-msvc\vulkan-1.lib >nul || exit /b 1
copy /y vulkan-1-x86.lib x86-msvc\vulkan-1.lib >nul || exit /b 1

echo == done ==
dir /b x64-msvc x86-msvc
exit /b 0

:make_def
REM %1 = dumpbin output file, %2 = .def file to write
(echo LIBRARY vulkan-1) > "%~2"
(echo EXPORTS) >> "%~2"
findstr /r /c:"^[ ]*[0-9][0-9A-Fa-f]* [ 0-9A-Fa-f]*vk" "%~1" > vk_lines_tmp.txt
for /f "tokens=4" %%A in (vk_lines_tmp.txt) do (echo %%A) >> "%~2"
del vk_lines_tmp.txt
goto :eof

