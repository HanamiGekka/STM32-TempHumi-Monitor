[English](README.md) | [简体中文](README.zh-CN.md) | [日本語](README.ja.md) | [繁體中文](README.zh-TW.md)

# TempHumi Monitor

A verifiable STM32F103C8T6 temperature/humidity acquisition system with a Qt desktop application, explicit per-channel sources, raw-data recording and Windows portable/Setup delivery.

Desktop **v010.18.1**; MCU firmware **v010.17.1**. Functional acquisition, source simulation and independent physical accuracy are separate results.

## Features

- Real temperature acquisition and three **provisional** MCU temperature-correction profiles; original and published values are retained.
- Explicit REAL/SIM selection for each MCU channel, including REAL temperature with MCU SIM humidity. REAL failures never silently become SIM.
- TH3 serial/file/REPLAY input, independent axes, Inspector, event inspection and SHA-256-verified full-session export.
- Pause Display leaves recording active; End Session finalizes files and releases the port. F1 opens offline Help; About distinguishes app/source and observed MCU.
- Recovery and confirmed-configuration restore default ON,5s between at most12attempts; fresh device identity/firmware/readback gates changing commands.

## Hardware and requirements

STM32F103C8T6, ADS1115, NTC/U6 conditioning, CHS1101/TLC556/OPA330 humidity design, OLED and CH340 USB-UART. The tested Windows package requires Windows10/11 x64 and the appropriate CH340 driver. Firmware programming uses ST-Link/SWD. The design render is not an as-built continuity certificate.

[Hardware evidence](docs/hardware/FINAL_TECHNICAL_AUDIT.md) · [Architecture](docs/architecture/OVERVIEW.md)

## Software architecture

HAL/platform -> BSP -> Driver -> pure Algorithm -> Application/source selection -> Protocol. OLED and UART use the same published semantics; Qt consumes results without recalibrating. REAL/REAL is the firmware boot mode.

## Build and firmware installation

Validated tools: Qt5.14.2/MinGW7.3x64, STM32CubeIDE1.19.0/Arm GCC13.3, Python3 and STM32CubeProgrammer. Import .project/.cproject or use the commands below. Before flashing, identify the target, verify power and image provenance, close acquisition and preserve a full Flash backup; load BIN at0x08000000, Verify and compare readback. Firmware fits64KiB Flash/20KiB RAM.

```powershell
python tools/build_desktop.py --configuration both --label verification --build-root build_verify/qt --evidence build_verify/qt_logs
python tools/build_mcu.py --build-root build_verify/mcu --evidence build_verify/mcu_logs
python tools/run_regressions.py --build-root build_verify/tests --evidence build_verify/test_logs --desktop-objects build_verify/qt/qt_debug/debug
```

[Build / flash instructions](docs/development/BUILD.md)

## Windows portable and Setup

Download the portable ZIP or unsigned Setup from the Release. Extract the complete ZIP before running TempHumi.exe, or install to a writable destination. Setup includes the same validated runtime, Start-menu entries and an optional desktop shortcut; it neither opens COM nor flashes firmware. Uninstall retains unowned data/settings. Keep all Qt libraries/plugins with the EXE.

[Release](https://github.com/HanamiGekka/STM32-TempHumi-Monitor/releases/tag/v010.18.1) · [Desktop guide](docs/development/DESKTOP.md)

## Acquisition and protocol

Identify the actual port;115200baud,8N1,no flow control. Startup prepares SERIAL without connecting. Choose a recording location and start; Apply Sources/Apply Temperature Fit require MCU confirmation. TH3 has23fields carrying raw codes, units, validity/errors, per-channel sources and generation. Display windows are5–1200s with at most24000points; raw/CSV recording is independent of display trimming.

[TH3](docs/protocol/TH2_TH3.md) · [UART commands](docs/protocol/UART_COMMANDS.md)

## Testing and validation

Fresh MCU/Qt Debug and Release builds have zero warnings/errors;22host regression suites,94native GUI checks at each100%/150% scale,120portable-package checks and102bundle checks pass. Separate portable and installed >=3600-second actual-hardware campaigns are required; current final status and counts are in the validation report. Sporadic preserved/recovered errors are reported, not hidden.

Supplied ten-pair temperature comparison: raw MAE0.754℃/RMSE1.020725℃; corrected MAE0.522℃/RMSE0.720777℃; maximum corrected absolute error1.53℃;8/10within±1℃. This is not a full-range metrological accuracy guarantee.

[Validation](docs/release/VALIDATION.md)

## Known limitations

- REAL humidity remains **PARTIAL / NOT VALIDATED**. Synthetic humidity is not evidence of measured RH accuracy.
- PWM waveform/frequency/duty are **NOT TESTED**; the near-zero V_CHS1101 test-point reading conflicts with follower/ADC DC readings.
- Physical USB dropout cause is **UNRESOLVED**. Recovery evidence does not prove a cable, supply or solder-joint diagnosis.
- Temperature profiles remain provisional; independent full-range accuracy, new optical OLED measurements and another physical Windows PC are not claimed.

## Release and source identity

Desktop v010.18.1 is built from the reviewed public source commit; MCU v010.17.1 remains byte-identical to the verified image. Fresh verification builds embed their own checkout/time and are not same-version replacements. The independent public history maps exact production-input hashes to the frozen source and asset SHA-256 values. Raw sessions, device identities, private history, Board images and account-bearing CAD databases are excluded.

[Source / asset identity](docs/release/SOURCE_IDENTITY.json)

## License and credits

Project-authored source/docs: **GPL-3.0-or-later**. QCustomPlot is by Emanuel Eichhammer; Qt/ST/Arm/MinGW retain upstream terms. The original student Qt contribution and hardware collaboration are credited as team contributions with owner-confirmed publication consent. Qt corresponding-source archives and runtime notices accompany the Release.

[LICENSE](LICENSE) · [NOTICE](NOTICE.md) · [Dependency terms](docs/release/THIRD_PARTY_NOTICES.md)
