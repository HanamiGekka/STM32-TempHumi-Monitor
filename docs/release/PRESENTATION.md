# Desktop presentation images

## Current released application — 2026-10-18

`desktop-v010.18.1-showcase.jpg` is an actual native Windows capture of the accepted portable **Qt v010.18.1** application. Its bytes are copied unchanged from the original capture: **1453 × 897**, JPEG, SHA-256 `223dab10e158235be60b511c0d1e209c167a2632f2c5fb51a35e0fdc4dd6f0c6`. No crop, curve, coordinate, legend, version, numerical value or status was edited. The JPEG encoding is the native capture format, not a renamed PNG; no EXIF metadata is present.

The EXE SHA-256 is `8412750e0fc6e3e30b9917c3e25d756b2153600230a69f63acfa1d685db84263`, matching the frozen accepted release. It reports application source `77f46e0b605097ddd851385bb893006a68685711`. USB CH340 UART used 115200 baud, 8N1, no flow control. Read-only STATUS/MODE/ID positively identified the connected board and firmware **v010.17.1** before any source/fit configuration. No firmware write, software rebuild or release replacement occurred. Device UID and raw session files remain private.

Visible state: **T:REAL / H:SIM**, **CONNECTED / VALID / DISPLAY LIVE**, MCU **Fit 2 / PROVISIONAL**. At the displayed source timestamp 1254420 ms, the private raw record contains corrected temperature 33.75 °C and simulated humidity 43.82 %RH; its matched MCU diagnostic contains raw temperature 32.054 °C and corrected 33.746 °C, consistent with the displayed 32.05 / 33.75. This correspondence verifies the displayed record, not sensor accuracy or optical OLED output.

The earlier part was recorded under room conditions; the user then moved the board near the computer exhaust. There was no independent reference-temperature measurement or controlled environmental step. Raw and corrected temperature share the left °C axis; blue humidity uses a separate right %RH axis. Vertical positions across these different units are not numerical comparisons. The extended negative humidity-axis range is a display setting, not negative measured humidity. Humidity is MCU simulation, not physically validated RH.

The retained clean capture was saved at **22:40:32 +08:00**, about **17 min 23 s** after launch. It was saved early at the user's request to preserve a usable image. The complete bounded recording ran from 22:23:11 to 22:48:11 +08:00 (1499.993 s). A later capture after 20 minutes was not obtained before the application's clean time limit. Do not relabel this image as a screenshot taken after 20 minutes. Source/fit changes and connection-recovery boundaries remain visible and in the private raw record; this was not an uninterrupted-connection benchmark. The final session has 1463 TH3 frames, 5632 raw records, five segments, zero malformed frames and no recording error. End Session released COM; the same identified board was restored to its initial REAL/REAL and Fit 1 with live readback, then COM closed.

An isolated preference file and a generic demo recording root protected ordinary user settings and previous sessions. The published image contains no device UID, personal path, private experimental material or other application window. The prior occluded capture remains local only.

This is an interface illustration, **not a new metrological accuracy test**. Existing REAL-humidity, PWM, provisional-temperature and unresolved USB physical-cause limitations remain unchanged. The [original v010.18.1 image](desktop-v010.18.1.png) and [historical v010.15.2 presentation image](desktop-showcase.png) remain byte-identical. Frozen release validation and binary identities remain authoritative.

## Historical presentation image — retained provenance

`desktop-showcase.png` is a maintainer-supplied historical presentation image selected for the README. Its visible UI version is **v010.15.2**; the current software release remains desktop **v010.18.1** / MCU **v010.17.1**.

The supplied image was copied byte-for-byte. Its capture/edit history has not been independently established, so it is a presentation reference, not a new measurement or release-acceptance record. No curve, coordinate, legend, version label or status text was edited in this maintenance task.

The visible mode is **T:REAL / H:SIM**. Humidity is MCU-simulated and uses the right axis; raw and corrected temperature use the left axis. The displayed humidity range extends below zero as a view setting; that does not imply negative measured relative humidity. The two axes have different units and their vertical positions cannot be compared numerically.

The original **v010.18.1** release screenshot, `desktop-v010.18.1.png`, is retained unchanged. The original release validation and frozen binary identities remain authoritative. The historical image does not prove current hardware connection, real humidity accuracy or the latest reconnect defaults.

SHA-256 of `desktop-showcase.png`: `be926114e7532d660ac51e198847c6e932f36f3f991cd8808a0b2326027d6572`.
