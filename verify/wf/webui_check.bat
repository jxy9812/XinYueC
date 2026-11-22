@echo off
setlocal
set PYTHONIOENCODING=utf-8
set PY=D:\anaconda3\python.exe
cd /d F:\Code\CMake\XinYueC
%PY% -u verify\webui\webui_check.py --server-port both
if errorlevel 1 exit /b 1
%PY% -u verify\webui\webui_hist_chart_check.py
exit /b %errorlevel%
