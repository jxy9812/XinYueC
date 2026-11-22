@echo off
setlocal
set PYTHONIOENCODING=utf-8
set PY=D:\anaconda3\python.exe
cd /d F:\Code\CMake\XinYueC
%PY% -u verify\webui\ref_sweep.py
exit /b %errorlevel%
