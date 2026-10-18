@echo off
setlocal EnableExtensions DisableDelayedExpansion
if exist "E:\Anaconda3\python.exe" set "TASK_PYTHON=E:\Anaconda3\python.exe"
if defined TASK_PYTHON goto run
if exist "C:\Anaconda3\python.exe" set "TASK_PYTHON=C:\Anaconda3\python.exe"
if defined TASK_PYTHON goto run
if exist "D:\Anaconda3\python.exe" set "TASK_PYTHON=D:\Anaconda3\python.exe"
if defined TASK_PYTHON goto run
echo [ERROR] Python not found on E, C or D. Run run_software_demo.py with an existing Python 3.
exit /b 1
:run
"%TASK_PYTHON%" -X utf8 "%~dp0run_software_demo.py" %*
exit /b %ERRORLEVEL%
