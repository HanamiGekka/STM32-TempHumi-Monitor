# UART control contract v1

## Additive v010.17.1 MCU identity

`CMD,ID?` is read-only. Reply: `RSP,ID,1,STM32F1,<24 lowercase hex characters>,<flash KiB>,<decimal device ID>`. Example fixture: `RSP,ID,1,STM32F1,0102030405060708090a0b0c,64,1040`. This is a synthetic example, not this board's UID. The 12 bytes follow ascending factory-memory address order at0x1FFFF7E8 (each HAL UID word little-endian); device1040 is0x410. Current desktop accepts the supported64/128KiB family values and hashes the exact12bytes with SHA-256. Raw UID/Flash/probe identifiers remain private.

New enum value is appended; existing command values and TH3/schema are unchanged. Older firmware may reject ID? and cannot satisfy automatic restore identity gates; it must not be declared safe by app version alone. UID is operational identity, not proof against clones. Pure formatter/parser vectors and actual UART-to-SWD byte comparison cover the representation. [ST RM0008 Rev21 section30.2](https://www.st.com/resource/en/reference_manual/cd00171190.pdf) and local HAL/UID_BASE define the byte source.

The last field is a raw optional **debug** device code. [ST ES096 Rev 15 section 2.2.2](https://www.st.com/resource/en/errata_sheet/cd00190234-stm32f101x8-b-stm32f102x8-b-and-stm32f103x8-b-medium-density-device-limitations-stmicroelectronics.pdf) documents unavailable debug registers in independent user-mode operation. The real board reproduced code `0` before any target SWD connection. Such a reply is accepted as an unavailable diagnostic, not as a zero factory UID: for example `RSP,ID,1,STM32F1,0102030405060708090a0b0c,64,0` is another synthetic fixture. The desktop accepts only code 0 or 1040 together with the existing valid factory UID/capacity/firmware and saved-target checks. Other nonzero device codes and invalid or mismatched UIDs fail. Events retain `dbg_device_id` and `dbg_device_id_available`; firmware does not manufacture 1040 or enable debugging to mask the condition. [validation summary](../release/VALIDATION.md).

115200 baud, 8N1, no flow control in recorded board tests. Commands are ASCII with LF or CRLF; maximum 63 bytes before LF including optional CR. Partial-line timeout is 1000 ms with uint32 wrap handling.

| Request | Reply / meaning |
|---|---|
| CMD,PING | ACK,PING |
| CMD,STATUS? | RSP,STATUS,1,firmware_version,uptime_ms,source,0 |
| CMD,MODE? | RSP,MODE,1,requested_generation,requested_T,requested_H,effective_generation,effective_T,effective_H |
| CMD,SET,TEMP,REAL or SIM | Request temperature source |
| CMD,SET,HUM,REAL or SIM | Request humidity source |
| CMD,SET,BOTH,REAL,SIM (any REAL/SIM pair) | Atomic channel-source request |
| Accepted SET | ACK,SET,requested_generation,requested_T,requested_H; accepted, not yet effective |
| Unavailable source | ERR,SOURCE_UNAVAILABLE; no state change |
| Unknown/bad/long/timeout input | ERR,UNKNOWN_COMMAND / BAD_BYTES / LINE_TOO_LONG / COMMAND_TIMEOUT |
| RX setup/queue/hardware fault | ERR,RX_INIT or ERR,UART_RX,mask |

The RX ISR queues bytes/rearms; main-loop parsing is bounded to 64 bytes per iteration over a 256-byte queue. Queue indices use Cortex-M3 atomic access/barriers; fault/reset handling briefly masks IRQ. Lost/bad input discards through LF. Errors are limited to 1 Hz. No blocking TX/formatting occurs in the ISR.

Qt allows one pending request, a 3000 ms deadline and explicit unconfirmed timeout/disconnect states. There is no transaction ID: late same-type responses cannot prove strict request association. Command sends are preserved in command_tx.jsonl; sent bytes alone do not prove execution. Confirm source changes with ACK plus MODE readback plus generation/source-consistent TH3. Reconnection queries MODE and does not resend SET.

STATUS/source validity is not sensor trust. [TH3](TH2_TH3.md), [validation summary](../release/VALIDATION.md), [validation summary](../release/VALIDATION.md).

RUN #3.1 keeps the final STATUS field at the literal `0` used by the current implementation; it is not the per-window calibration identifier. Read TH3 calibration_id for the provisional correction (1) or uncorrected result (0). BOOT/STATUS use the unified project VERSION.

## v010.14.3 provisional calibration selection

`CMD,CAL,0` disables correction; `CMD,CAL,1` selects the retained ambient offset; 2 selects guarded two-segment LAD, 3 guarded six-segment Huber. `ACK,CAL,id` acknowledges a request only. `CMD,CAL?` returns `RSP,CAL,1,requested,effective`. A choice takes effect after the current window is published, in main-loop context. It is not stored in Flash; boot defaults to1 and REAL/REAL. Reconnection never resends CAL. Unknown IDs/commands return the existing UNKNOWN_COMMAND error without changing state.

Confirm ACK, requested/effective readback and the matching per-window `[TEMP_CORR]` profile, timestamp, calibration ID and TH3 value. A requested profile can be inactive (TH3 ID0) for SIM, invalid or out-of-support data. All profiles are provisional; none certify temperature/humidity accuracy. Older firmware may reject CAL; Qt records the error and never locally invents a correction.

## OLED temperature evidence in the completed v010.14.3 build

The OLED receives the same already-corrected application snapshot as TH3. The numeric row uses one decimal; TH3 uses two. `CAL:1/2/3 PROV` means provisional correction applied, `CAL:n BASE` means the configured method is inactive at this input, and `CAL:NONE` means Off. SIM is explicitly labeled and has no empirical correction. Invalid temperature/humidity displays `NA`, including finite values with cleared validity flags. Existing glyph size and REAL diagnostic-page rotation remain unchanged; the numeric REAL page appears once every three windows. MIXED/SIM pages show the temperature each window.

One bounded `[OLED_TEMP] t_ms=... source=REAL/SIM profile=... cal=... temperature=...C text=T:...C shown=0/1 status=...` follows the matching TH3 and OLED write attempt. `shown=1,status=0` means every driver/HAL write for that numeric page succeeded. Other REAL diagnostic pages have shown0. Nonzero status is a display transport error; no retry, sample substitution or change to TH3 is hidden. The diagnostic preserves a three-decimal intended value and the exact one-decimal glyph text. It verifies rendering/transport, not optical pixels or measurement accuracy. Its bounded UART cost is at most one224-byte buffer/window (under20ms at115200baud/8N1); actual publish timing/group counts are separately checked. Earlier v010.14.3 builds lack this additive diagnostic and retain their own image hashes.

## v010.15.1 reconnect and recorder contract

Wire TH3 remains23 fields; command syntax, validity, source generations,16 groups, OLED shared snapshot and CSV3 fields are unchanged. Qt reopens only through bounded uniquely identified recovery or an explicit selected-port action. It waits for a fresh valid TH3 before read-only MODE?/STATUS?/CAL?; it does not replay SET/CAL. Prior configuration/firmware evidence is unconfirmed until the current connection supplies it. For SIM, CAL readback can confirm configuration while applied correction stays inactive and the physical original stays N/A.

The same recorder appends connection/segment events to session_events.jsonl with actual UTC, numeric Qt error and actions; unknown source time stays null. No row is synthesized for missing time. CSV timestamps remain the actual source/segment values even where graph X uses a composite recovery coordinate. End cancels all recovery/query work and closes the session. Disk/metadata/command-log failures are explicit recording errors. No protocol migration or historical-session rewrite is needed.
