@echo off
setlocal EnableExtensions DisableDelayedExpansion
cd /d "%~dp0"
if errorlevel 1 exit /b 1

if not exist "bin\simulation_stream.exe" (
    echo [INFO] Simulator EXE is missing. Building from the project C sources...
    call "%~dp0BUILD_SIMULATION.bat"
    if errorlevel 1 (
        echo [ERROR] No simulation stream is available.
        pause
        exit /b 1
    )
)

echo [SIM] TH3 frames are printed below and written to output\realtime_th3.txt.
echo [SIM] This is software-generated data. Press Ctrl+C to stop.
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -ExecutionPolicy Bypass -File "%~dp0STREAM_TO_FILE.ps1" ^
    -ExecutablePath "%~dp0bin\simulation_stream.exe" ^
    -OutputPath "%~dp0output\realtime_th3.txt"
set "SIM_EXIT=%ERRORLEVEL%"
echo.
echo [SIM] Stream stopped. Exit code: %SIM_EXIT%
pause
exit /b %SIM_EXIT%
