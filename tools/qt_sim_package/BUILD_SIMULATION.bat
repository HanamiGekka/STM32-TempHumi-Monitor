@echo off
setlocal EnableExtensions DisableDelayedExpansion

for %%I in ("%~dp0..\..") do set "PROJECT_ROOT=%%~fI"
if not exist "%PROJECT_ROOT%\tools\host_src\simulation_stream.c" (
    echo [ERROR] C sources are missing. Rebuild requires the complete project checkout.
    exit /b 1
)

where gcc.exe >nul 2>&1
if errorlevel 1 (
    echo [ERROR] gcc.exe was not found on PATH. Install a Windows GCC toolchain to rebuild.
    exit /b 1
)

if not exist "%~dp0bin" mkdir "%~dp0bin"
if errorlevel 1 exit /b 1

echo [BUILD] Compiling the existing C simulation source and TH3 formatter...
gcc -std=c11 -O2 -Wall -Wextra -Werror -static-libgcc -include stddef.h ^
    -I "%PROJECT_ROOT%\Core\Inc" ^
    "%PROJECT_ROOT%\tools\host_src\simulation_stream.c" ^
    "%PROJECT_ROOT%\Core\Src\app_measurement_source.c" ^
    "%PROJECT_ROOT%\Core\Src\app_measurement_source_sim.c" ^
    "%PROJECT_ROOT%\Core\Src\app_protocol.c" ^
    "%PROJECT_ROOT%\Core\Src\algo_sensor_convert.c" ^
    -lm -o "%~dp0bin\simulation_stream.exe"
if errorlevel 1 (
    echo [ERROR] Host simulation build failed.
    exit /b 1
)
echo [BUILD] Ready: bin\simulation_stream.exe
exit /b 0
