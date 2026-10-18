# Build, verify and flash

Validated Windows toolchains: Qt5.14.2 with MinGW7.3x64 and STM32CubeIDE1.19.0 with Arm GCC13.3.rel1. Python3 orchestrates existing tools. E/C/D Qt discovery is supported; set TEMP_HUMI_QT_ROOT or STM32CUBEIDE_ROOT for another compatible installation. Development requires the listed older kit; hosted CI does not install or validate it.

```powershell
python tools/build_desktop.py --configuration both --label verification --build-root build_verify/qt --evidence build_verify/qt_logs
python tools/build_mcu.py --build-root build_verify/mcu --evidence build_verify/mcu_logs
python tools/run_regressions.py --build-root build_verify/tests --evidence build_verify/test_logs --desktop-objects build_verify/qt/qt_debug/debug
python tools/run_recovery_policy.py --output build_verify/policy
python tools/tests/check_oled_temperature.py --output build_verify/oled
```

Use new output directories. Debug/Release/make definitions are retained as build inputs; compiler objects and binaries stay ignored. The public make definitions use PROJECT_ROOT rather than a developer's private absolute linker path. CubeIDE can also import .project/.cproject and regenerate configurations. Do not regenerate CubeMX without reviewing protected regions and IOC differences.

For portable host-only CI, use `python tools/run_regressions.py --host-only --build-root build_verify/host --evidence build_verify/host_logs` with GCC/CC available. Fixtures are REPLAY/SIM, including constructed REAL tokens.

## Firmware installation

Connect SWD only to the intended STM32F103C8T6 board, with correct ground, SWDIO and SWCLK. Use a compatible ST-Link and STM32CubeProgrammer. Verify target identity, supported capacity and stable supply; close any acquisition/debugger session first. Read and retain a full Flash backup before programming. BIN load address is0x08000000; the release fits64KiB Flash/20KiB RAM. Program the named MCU BIN or addressed HEX, enable Verify, read the programmed region back and compare bytes/SHA-256, then reset and check BOOT/STATUS/TH3. Never flash a desktop EXE or use a foreign board's identity/backup.

The capture helper can control a selected port, but a normal app launch or installer never flashes firmware. Hardware programming requires explicit local target evidence. Build PASS and Flash Verify are separate from sensor accuracy.

## Build identity

Fresh Qt builds embed their checkout commit and compile time, so their hashes differ from frozen historical release EXEs. The production input hashes and formal assets are mapped in [source identity](../release/SOURCE_IDENTITY.json); verification builds are not silently promoted as same-version release replacements. Physical SWD/UART and proprietary Windows kit validation are local gates, not GitHub-hosted CI.
