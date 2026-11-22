@echo off
setlocal
set PYTHONIOENCODING=utf-8
set PY=D:\anaconda3\python.exe
cd /d F:\Code\CMake\XinYueC\HighSpeedFilter\server_py
%PY% -u tests/test_vfd_params.py
if errorlevel 1 exit /b 1
cd /d F:\Code\CMake\XinYueC\HighSpeedFilter\client_py
%PY% -u tests/test_hsf_regression.py --only=remote
exit /b %errorlevel%
