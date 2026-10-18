# Validation of desktop v010.18.1 / MCU v010.17.1

The frozen binaries were built from public commit `77f46e0b605097ddd851385bb893006a68685711` with exact Git-blob
bytes. The release tag adds final documentation and reviewed summaries; all160
production inputs remain unchanged. [Machine summary](VALIDATION.json) and
[source/asset mapping](SOURCE_IDENTITY.json) give the reproducible identities.

## Separate real distribution campaigns

Each exact frozen EXE ran from its own portable extraction or actual Setup
installation with a System32-only PATH/working directory and isolated preferences.
The target supplied REAL temperature and explicitly selected MCU SIM humidity,
provisional profile3. Each campaign requested3665process seconds and independently
passed >=3600host-session and forward-source seconds. Counter rollbacks are not
counted as giant forward intervals. Both ended the recording, closed COM and
restored the observed pretest volatile configuration.

| Distribution | Host seconds | Forward-source seconds | TH3 frames | Byte-anomaly records | Counter rollbacks | Max forward gap, s |
|---|---:|---:|---:|---:|---:|---:|
| Portable | 3665.000 | 3664.149 | 3611 | 0 | 0 | 1.015 |
| Installed | 3664.995 | 3664.148 | 3605 | 1 | 0 | 7.105 |

Predeclared acceptance requires >=95% expected frame coverage, >=95% usable
canonical frames, <=1% malformed frames, no forward gap over120s, exact raw
offset/CSV/metadata consistency and clean session finalization. Sporadic anomalies
and recovered loss are retained. Missing original/OLED diagnostic pairs are
reported separately; they are never invented as matches or treated as physical
accuracy evidence. These thresholds were fixed before the campaigns.

The portable recording reconciles all3611
original/model pairs and3611OLED diagnostics.
The installed recording reconciles3604
of3605original/model pairs and all3605OLED diagnostics;
one original diagnostic is unavailable across a retained interruption. Its digital
reconciliation is **PARTIAL**, while its operational campaign passes the declared
quality policy. One actual serial disconnect/reopen and confirmed restoration
occurred; the7.105s largest forward gap is retained, with no counter rollback.
The legacy newline-based strict auditor retains **FAIL** (10827CSV byte-boundary
records versus10826whole-file newline fragments). The byte-offset audit preserves
that segment boundary and does not fabricate the missing original. No raw file,
parser tolerance, calibration constant or measurement value was altered to remove
these outcomes. The physical cause of the interruption remains unresolved.

## Builds and software

- MCU Debug/Release and Qt Debug/Release: zero errors/warnings.
- One final authorized MCU program/Verify/full64KiB readback/runtime PASS.
  Fresh public MCU builds reproduce the programmed BIN/HEX; packaging made no
  further firmware write and changed no option/protection bits.
- 22 host regression suites;94native Windows GUI checks at each100%/150%;
  120 portable checks and102 bundle checks PASS.
- Actual UART/recovery/control probe:57checks PASS. Explicit simulated resource faults
  and contention remain labeled; they do not prove a physical USB cause.
- Profile/source GUI probe:78checks PASS, with310s requested stability.
  Recovery policy147vectors, OLED2565checks, dense temperature3333079checks in
  each enabled/disabled mode and147supplied profile rows pass.
- Additional unchanged-recorder fixture evidence covers634checks,2662field
  comparisons,6sessions,3I/O failures and2native metadata-lock cases. Its earlier
  current-run17.1 invocation is reused for unchanged recorder behavior; the22
  canonical18.1 suites also exercise the current parser/recorder objects.
- Setup actually installs, starts directly/from its shortcut, opens offline Help,
  records to a writable directory, uses9local runtime modules and exits normally.
  Final installed hardware campaign passes independently. Normal QA uninstall
  retains unowned synthetic data, native preferences and the previous installation.
- windeployqt exit1 is retained. Explicit deployment from the same validated kit,
  zero unresolved imports and actual runtime-module origin checks pass. Setup is
  unsigned. Another physical Windows PC was not tested.

Hosted CI checks the public allowlist, links, syntax and portable C host vectors.
Windows kit builds, native GUI and physical hardware tests are local evidence;
CI does not claim those tests ran on GitHub.

## Measurement limits

The [ten-pair CSV](temperature_comparison.csv) permits recalculation: raw
MAE0.754C/RMSE1.020725C; corrected MAE0.522C/RMSE0.720777C; maximum corrected
absolute error1.53C,8/10within +/-1C. The user supplied these observations;
reference uncertainty and equilibration are unspecified. This is not a full-range
metrological calibration or a +/-1C guarantee.

REAL humidity remains PARTIAL/NOT VALIDATED. PWM waveform/frequency/duty remain
NOT TESTED. Near-zero V_CHS1101 conflicts with the reported follower/ADC values.
USB physical root cause remains UNRESOLVED. New optical OLED measurement,
independent supply/oscilloscope instruments and another Windows PC are unperformed.
See the [technical audit](../hardware/FINAL_TECHNICAL_AUDIT.md).

## Public evidence boundary

This summary is derived from exact retained private raw bytes and completed
test receipts. Original user data, real device identities, Flash images,
private development history and account-bearing CAD databases are excluded.
The real application screenshot shows its explicit REAL/SIM classification.
No private source ancestry is imported into this public repository.
