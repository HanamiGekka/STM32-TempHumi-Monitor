# Desktop installation and acquisition

Windows10/11 x64: download the v010.18.1 portable ZIP or unsigned Setup from Releases. Extract the whole portable folder before running TempHumi.exe. Setup installs the same accepted desktop/runtime bytes, provides Start-menu entries and an optional desktop shortcut. It prefers an E-drive destination when available. Install to a writable directory; normal installation does not launch acquisition or flash a board. Uninstall retains unowned data and native preferences.

On startup SERIAL is prepared without opening a port. Identify the actual CH340 port, choose115200/8N1/no flow control, select a writable recording location and start. Source/fit changes require explicit Apply and MCU confirmation. REAL temperature plus MCU SIM humidity is an explicitly mixed mode; humidity simulation does not validate the real humidity circuit.

Raw temperature is red, corrected temperature orange on the shared left axis; humidity is blue on the right. Source-time plots retain at most24000display points and support5–1200s windows. Pause Display leaves reception/recording active. End Session finalizes raw_input.log, parsed.csv, metadata.json, session_events.jsonl and command_tx.jsonl, then releases COM. Missing or invalid physical values remain unavailable; there is no silent SIM fallback.

F1 opens searchable offline Help; About separates desktop/source identity from observed MCU firmware. Main-window Esc opens a default-No Exit confirmation. Recovery/restoration defaultON, with5s intervals and at most12attempts. Fresh same-device UID/firmware/readbacks gate restoration of confirmed settings. Explicit OFF persists. Inspect data/event points without altering original data. Full-session export verifies sizes/SHA-256 independently of display trimming.

Keep raw sessions private unless intentionally reviewed for sharing. [Validation and known limits](../release/VALIDATION.md).
