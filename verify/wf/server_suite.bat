@echo off
setlocal
set PYTHONIOENCODING=utf-8
set PY=D:\anaconda3\python.exe
cd /d F:\Code\CMake\XinYueC\HighSpeedFilter\server_py
%PY% -u tests/run_all.py
exit /b %errorlevel%
