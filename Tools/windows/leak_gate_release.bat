@echo off
rem Session leak gate against the RELEASE server (bin-release\HsfServer.exe).
rem ASCII only. Two rounds; exit 0 = both rounds zero heap growth.
set HSF_LEAK_SERVER_EXE=F:\Code\CMake\XinYueC\bin-release\HsfServer.exe
py -3 F:\Code\CMake\XinYueC\verify\hsf_leak_gate.py
exit /b %ERRORLEVEL%
