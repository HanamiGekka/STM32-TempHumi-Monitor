@echo off
setlocal EnableExtensions

set "ACTION=%~1"
if "%ACTION%"=="" set "ACTION=build"

set "CONFIG=Release"
set "PROJECT_NAME=STM32_TempHumi_Monitor"
for %%I in ("%~dp0..") do set "PROJECT_ROOT=%%~fI"

set "DEFAULT_CUBEIDE_ROOT=E:\ST\STM32CubeIDE_1.19.0\STM32CubeIDE"
if defined STM32CUBEIDE_ROOT (
  set "CUBEIDE_ROOT=%STM32CUBEIDE_ROOT%"
) else (
  set "CUBEIDE_ROOT=%DEFAULT_CUBEIDE_ROOT%"
)

set "HEADLESS_BUILD=%CUBEIDE_ROOT%\headless-build.bat"
set "WORKSPACE_DIR=%TEMP%\STM32CubeIDE-Headless-%PROJECT_NAME%"

if not exist "%HEADLESS_BUILD%" (
  echo [ERROR] headless-build.bat not found: "%HEADLESS_BUILD%"
  echo [HINT] If your CubeIDE is installed elsewhere, set STM32CUBEIDE_ROOT first.
  exit /b 1
)

if /I "%ACTION%"=="build" goto build
if /I "%ACTION%"=="clean" goto clean

echo Usage: %~nx0 [build^|clean]
exit /b 1

:build
echo [INFO] CubeIDE root: "%CUBEIDE_ROOT%"
echo [INFO] Headless workspace: "%WORKSPACE_DIR%"
echo [INFO] Building: "%PROJECT_NAME%/%CONFIG%"
call "%HEADLESS_BUILD%" -data "%WORKSPACE_DIR%" -import "%PROJECT_ROOT%" -build "%PROJECT_NAME%/%CONFIG%" -markerType all -printErrorMarkers -verbose -no-indexer
exit /b %ERRORLEVEL%

:clean
call :resolve_tool_bins
if errorlevel 1 exit /b 1

if not exist "%PROJECT_ROOT%\%CONFIG%\makefile" (
  echo [INFO] "%PROJECT_ROOT%\%CONFIG%\makefile" not found. Nothing to clean.
  exit /b 0
)

set "PATH=%MAKE_TOOLS_BIN%;%GNU_TOOLS_BIN%;%PATH%"
echo [INFO] Cleaning: "%PROJECT_NAME%/%CONFIG%"
pushd "%PROJECT_ROOT%\%CONFIG%"
make.exe -j%NUMBER_OF_PROCESSORS% clean
set "EXIT_CODE=%ERRORLEVEL%"
popd
exit /b %EXIT_CODE%

:resolve_tool_bins
set "GNU_TOOLS_BIN="
for /d %%I in ("%CUBEIDE_ROOT%\plugins\com.st.stm32cube.ide.mcu.externaltools.gnu-tools-for-stm32.*") do set "GNU_TOOLS_BIN=%%~fI\tools\bin"

set "MAKE_TOOLS_BIN="
for /d %%I in ("%CUBEIDE_ROOT%\plugins\com.st.stm32cube.ide.mcu.externaltools.make.win32_*") do set "MAKE_TOOLS_BIN=%%~fI\tools\bin"

if not exist "%GNU_TOOLS_BIN%\arm-none-eabi-gcc.exe" (
  echo [ERROR] arm-none-eabi-gcc.exe not found under "%CUBEIDE_ROOT%\plugins".
  echo [HINT] Confirm CubeIDE root or plugin layout.
  exit /b 1
)

if not exist "%MAKE_TOOLS_BIN%\make.exe" (
  echo [ERROR] make.exe not found under "%CUBEIDE_ROOT%\plugins".
  echo [HINT] Confirm CubeIDE root or plugin layout.
  exit /b 1
)

exit /b 0
