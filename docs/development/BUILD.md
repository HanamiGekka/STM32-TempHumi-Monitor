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

## Rebuilding a local portable package and Setup

Use a fresh output directory and the receipts from your own checkout. The current
desktop version is 18.1 while the unchanged MCU version is 17.1. The installer
accepts an explicit local identity plus both fresh build receipts; it does not
require access to private experiment files. Local rebuilds embed their own
commit/time and are not replacements for frozen official Release assets.
After the build commands above with label verification, run:

```powershell
python tools/package_desktop.py --build-summary build_verify/qt_logs/verification_summary.json --evidence build_verify/package_logs --destination dist/local/v010.18.1 --zip
```

Create a local identity from the same receipts and actual package bytes:

```powershell
@'
from pathlib import Path
import hashlib, json
root = Path.cwd()
qt = json.loads((root / "build_verify/qt_logs/verification_summary.json").read_text(encoding="utf-8"))
mcu = json.loads((root / "build_verify/mcu_logs/mcu_build_summary.json").read_text(encoding="utf-8"))
assert qt["pass"] and mcu["pass"]
release = next(item for item in mcu["builds"] if item["configuration"] == "release")
version = qt["version"]
sha = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
identity = {
    "version": version,
    "firmware_version": mcu["version"],
    "source_commit": qt["head"],
    "qt_exe_sha256": sha(root / "dist/local" / version / "TempHumi.exe"),
    "desktop_zip_sha256": sha(root / "dist/local" / ("TempHumi_" + version + "_win64.zip")),
    "mcu_bin_sha256": release["bin_sha256"],
    "mcu_hex_sha256": release["hex_sha256"],
}
(root / "dist/local/identity.json").write_text(json.dumps(identity, indent=2) + "\n", encoding="utf-8")
'@ | python -X utf8 -
python tools/build_installer.py --identity dist/local/identity.json --desktop dist/local/v010.18.1 --portable-zip dist/local/TempHumi_v010.18.1_win64.zip --build-summary build_verify/qt_logs/verification_summary.json --firmware-build-summary build_verify/mcu_logs/mcu_build_summary.json --output dist/local --evidence build_verify/installer_logs
```

Inno Setup 7 is an additional build prerequisite; its compatible compiler is
searched on E, C, then D. Preserve dependency notices and Qt corresponding
sources when distributing a rebuilt runtime. Package checks do not replace
actual install/start/record/close/uninstall or board acceptance. Review local
manifests and runtime data for privacy before sharing.
