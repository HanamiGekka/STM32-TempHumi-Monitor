# TH3 measurement contract

The active software protocol is **TH3 only**. This stable document path remains for links/history. DEC-20261014-TH3-ONLY supersedes TH2 compatibility; completed experiments are unchanged.

## Frame and validation

ASCII records normally end in CRLF. The 23 positions are:

```text
TH3,source,timestamp_ms,valid,valid_flags,error_flags,calibration_id,sample_count,temperature_c,humidity_rh,v_ntc,v_chs1101,rh_pwm,rh_adc,raw_ntc_code,raw_chs1101_code,pwm_period_ticks,pwm_high_ticks,temperature_source,humidity_source,temperature_valid,humidity_valid,mode_generation
TH3,SIM,0,1,3,0,0,1,29.00,60.00,NA,NA,NA,NA,NA,NA,NA,NA,SIM,SIM,1,1,0
TH3,MIXED,1000,1,71,0,0,16,24.39,60.00,1.477,NA,NA,NA,11817,NA,NA,NA,REAL,SIM,1,1,1
TH3,REAL,2000,0,205,48,0,16,24.39,NA,1.477,1.234,NA,NA,11817,9851,NA,NA,REAL,REAL,1,0,2
```

Each source is REAL/SIM; overall source is their common source or MIXED. Channel validity agrees with flags/presence; global valid is their AND. SIM physical fields are NA and physical flags cleared. REAL failures remain errors with no fallback or recalibration by Qt. Numeric presence does not establish accuracy.

Timestamp is uint32 producer milliseconds; rollover is unwrapped within a segment. Rollback, source/generation changes and transport resets start separate segments and invalidate cursors. Generation zero is initial source state; subsequent SET increments skip zero on wrap. Units: deg C, %RH, V, int16 ADC codes and uint32 TIM2 ticks. Calibration ID 0 means no established calibration. Window averages and last raw sample are not necessarily simultaneous. STM32 publishes about every 1000 ms; host SIM about every 100 ms.

## Source versus flashed image

Qt/CLI classify TH2 and old TH measurement records as UNSUPPORTED_LEGACY_PROTOCOL, preserving raw bytes and rejection rows; these never become measurements. Human command replies remain diagnostics.

Historical MCU source **v0.6.1** and current unified firmware emit TH3 at boot. RUN #2.5 observed **v0.6.0** booting TH2 and starting TH3 after explicit Apply Sources/SET; later user-recorded BOOT/STATUS declared v0.6.1 with historically unknown programming provenance. RUN #3.1 subsequently programmed and verified the current unified image, whose exact identity is in [release identity](../release/VALIDATION.md). Connection sends MODE only, never automatic SET. No flash occurred in RUN #2.5 or #3.0. ACK, MODE readback and matching TH3 sources/generation establish source-control confirmation. [Commands](UART_COMMANDS.md).

## Recording migration and events

Historical CSV schema2 retains its original column order and producer lexemes/raw offsets. New v010.14.3 sessions use the additive schema3 described below. `parsed_th2_count` remains present for old readers and is always zero in new sessions. Old CSV/metadata/raw are unchanged and identified archive directories remain openable/exportable; replaying old TH2 raw never silently translates it or restores compatibility.

Append-only `session_events.jsonl` is separate from measurement bytes. Event schema 1 records event_index, event_type, host_event_timestamp (UTC), host_label (local time), source_time_known, applicable source/unwrapped timestamp, elapsed seconds, segment_id, generation and session_id. Unknown source fields are null. Metadata references the event file/count. Export includes its SHA-256; old sessions without an event reference remain supported. Raw/CSV contain no marker frames.

File REAL stays REPLAY/FIXTURE. [validation summary](../release/VALIDATION.md) separates software, physical UART and unresolved sensor accuracy.

## RUN #3.1 correction semantics (wire-compatible)

Version strings use unified root VERSION; 23 TH3 fields/CSV schema remain unchanged. calibration_id=1 denotes only a provisional valid REAL-temperature window-mean adjustment, not certified accuracy or humidity calibration; 0 denotes uncorrected/invalid/SIM/outside-domain temperature. A bounded human `[TEMP_CORR]` diagnostic preserves original physical mean float32 bits plus decimal raw/corrected/offset; it remains a diagnostic raw/CSV row. ADC code and voltage are unchanged. Correction occurs once after averaging; Qt never corrects values. [validation summary](../release/VALIDATION.md).

## v010.14.3 supplemental original-temperature evidence

Wire TH3 stays23 fields. ID0 is inactive/uncorrected; IDs1/2/3 identify provisional MCU temperature correction profiles. A timestamped human diagnostic appends `t_ms` and configured `profile` to raw/corrected/offset, original raw float32 bits, active calibration ID and state. It is emitted immediately before the corresponding REAL-temperature TH3 window.

Qt matches a diagnostic once to the next parsed TH3 only when timestamp, active calibration ID and corrected value agree within output rounding. Raw decimal and offset checks reject inconsistent diagnostics. Missing/old/mismatched evidence stays unavailable; the latest ADC cannot substitute for an averaged physical temperature. For uncorrected ID0 frames the plotted red value can use the producer's uncorrected TH3 value; simulated values remain labeled SIM.

CSV schema3 preserves all schema2 columns and appends physical_temperature_c (exact float32 mean rendered with17 significant digits), correction_profile and correction_pair_status. Raw bytes and original published temperature_c remain intact. Missing values are NA. Old recordings are preserved/exportable. `session_events.jsonl` adds CORRECTION_PROFILE_CHANGED with correction_profile; response-time analysis spanning multiple observed profiles is invalidated as MIXED_CORRECTION_PROFILES. Display visibility does not alter reception/recording.
