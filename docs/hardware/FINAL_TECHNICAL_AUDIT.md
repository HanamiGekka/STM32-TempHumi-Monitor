# Final technical audit and course-report evidence

2026-10-18, Asia/Shanghai. RUN #4.4; project/firmware/desktop v010.17.1.
Production source: `f3d11011a3a6b42390839f16cd63f0f16abbdbdd`;
fresh verification checkout baseline: `96415c7ea5fcc522b3a011dc7e33b80d0cf40719`.
Board label withheld from the public summary. This document is the reusable technical account;
the [validation summary](../release/VALIDATION.md)
indexes original evidence and current software-test results.

## System and requirements

The STM32F103C8T6 reads two ADS1115 channels, publishes explicit source/validity/error
semantics over UART, and drives an OLED from the same published measurement snapshot.
The Qt application records exact incoming bytes, classified CSV rows, metadata,
commands and events independently of its bounded display. REAL is the firmware
boot default. MCU simulation is selected explicitly per channel; desktop replay or
growing-file simulation is separately labeled. A faulty REAL channel never becomes
SIM automatically. The [architecture](../architecture/OVERVIEW.md) and
[TH3 specification](../protocol/TH2_TH3.md) define these implemented boundaries.

The hardware design includes an NTC, an SGM8541 temperature conditioning stage,
ADS1115, CHS1101, TLC556, OPA330, CH340, AMS1117 and OLED. Component identities in
the schematic are design evidence; board photos and a project container are not
a measured as-built netlist. Reference requirements calling the temperature stage
OPA330 or the ADS package a different suffix must not replace the actual design
labels silently. No hardware, calibration constant or physical conversion formula
was changed in this audit.

## Evidence and instruments

- `[confirmed]` The original schematic PDF was rendered and visually inspected.
  Its temperature stage is U6 SGM8541XC5/TR, R10 8.2 kohm, R11 10 kohm,
  R12 61.9 kohm; humidity uses U7 TLC556IDR and U9 OPA330AIDBVR.
- `[document claim]` Voltage and ten paired temperature observations were supplied
  by the user in the current task brief. They were not newly measured by the agent.
  Meter make/model, range, probe/ground arrangement, board/BOM revision,
  equilibration time and reference uncertainty were not supplied. Treat their
  precision as reported digits, not a calibrated uncertainty.
- `[confirmed]` Original Qt sessions were audited read-only using metadata, raw byte
  offsets, CSV and sidecars. SERIAL origin and per-channel sources were checked.
- `[confirmed]` The LCEDA project is a SQLite EPRJ2 container with design-history
  records and account/membership information. It was opened read-only. Its
  original bytes remain private; opaque history payloads do not establish
  present physical connectivity or justify publishing embedded account data.
- `[confirmed]` Fresh programming uses ST-Link/CubeProgrammer and two instantaneous
  target-voltage readings. These are not new independent 5 V/3.3 V DMM measurements.
  No oscilloscope, automated power instrument or independent humidity chamber
  was available or used.

## Temperature chain

The design path is NTC divider -> U6 non-inverting stage -> R8/filter -> ADS1115
AIN0 -> signed ADC conversion -> physical NTC model -> optional provisional
MCU correction -> shared OLED/TH3 result -> Qt. The current
source conversion in Core/Src/algo_sensor_convert.c distinguishes per-group conversion, window means
and latest raw code. Qt does not perform a second temperature calibration.

`[document claim]` NTC/U6 pin3 is 1.25 V, U6 output 1.45 V and AIN0 1.44 V.
`[confirmed, calculation]` Design gain is `1 + 10/61.9 = 1.161551`; multiplying
1.25 V gives 1.45194 V, consistent with the reported output to the quoted digits.
The output-to-AIN0 difference is about 10 mV; without simultaneous readings,
meter uncertainty and measured loading, it is not a quantified offset diagnosis.

`[conflict]` The drawing's R10 is 8.2 kohm while the existing physical model uses
its separately documented nominal fixed resistance. Historical text also names
other divider values. These are not an as-built resistor measurement. No constant
was adjusted to conceal this difference. The manufacturer's older R-T table
ordering conflict remains in the existing issue record.

The ADS1115 driver expresses a +/-4.096 V PGA range and signed 16-bit conversion,
`V = code * 0.000125 V`. PGA range is not permission to exceed the input supply
rails. The configured 250 SPS converter rate, approximately 16 groups per second
and approximately one-second published window are distinct rates.

## Ten independent supplied temperature pairs

Signed error is DUT minus reference, in deg C. All ten pairs are retained;
there is no removal of inconvenient samples and no new fit.

| Pair | Reference | Raw | Corrected | Raw error | Corrected error |
|---:|---:|---:|---:|---:|---:|
| 1 | 24.90 | 24.18 | 24.18 | -0.72 | -0.72 |
| 2 | 27.10 | 25.64 | 26.58 | -1.46 | -0.52 |
| 3 | 25.10 | 24.82 | 24.82 | -0.28 | -0.28 |
| 4 | 25.30 | 25.18 | 25.18 | -0.12 | -0.12 |
| 5 | 29.50 | 27.23 | 27.97 | -2.27 | -1.53 |
| 6 | 26.30 | 24.96 | 24.96 | -1.34 | -1.34 |
| 7 | 25.00 | 25.05 | 25.05 | +0.05 | +0.05 |
| 8 | 24.90 | 25.10 | 25.10 | +0.20 | +0.20 |
| 9 | 25.50 | 25.15 | 25.15 | -0.35 | -0.35 |
| 10 | 27.80 | 27.05 | 27.69 | -0.75 | -0.11 |

| Statistic | Raw | Corrected |
|---|---:|---:|
| Mean signed error, deg C | -0.704 | -0.472 |
| MAE, deg C | 0.754 | 0.522 |
| RMSE, deg C | 1.020725 | 0.720777 |
| Maximum absolute error, deg C | 2.27 | 1.53 |
| Within +/-1 deg C | 7/10 | 8/10 |

MAE improves by 30.7692%; RMSE improves by approximately 29.39%.
The machine-readable [reviewed validation data](../release/VALIDATION.md)
and [reviewed validation data](../release/VALIDATION.md)
allow independent recalculation. This small user-observed comparison does not
establish equilibrium, traceability, full-range accuracy or a +/-1 deg C guarantee.

## Humidity DC consistency and unresolved conversion

`[document claim]` U7 pin9/H_PWM average is 1.925-1.927 V; H_PWM_OUTPUT was reported
as 0.801-1.300 V at other observations; PA0 normally 1.326-1.327 V; H_DC 1.284 V;
U9 +IN 1.280 V; U9 OUT 1.280-1.282 V; ADS AIN1 1.280-1.281 V;
V_CHS1101 test-point reading 0.001-0.003 V; reference humidity approximately 57-60%RH.

`[confirmed, calculation]` Using the measured midpoint 1.926 V and design values:

```text
PA0 average = 1.926 * 2.2 / (1 + 2.2) = 1.324125 V
H_DC        = 1.926 * 20 / (10 + 20) = 1.284000 V
```

The PA0 reported normal average differs from the calculated divider by about
1.9-2.9 mV; H_DC matches the quoted divider result. U9 input/output/AIN1 agree
within the few reported millivolts. These checks support local DC consistency,
not humidity accuracy or dynamic PWM operation.

`[conflict]` In the drawing, H_PWM_OUTPUT feeds PA0; a separately reported
0.801-1.300 V value cannot automatically be equated to the normal 1.326 V reading
without matching time/test-point/ground conditions. Likewise the V_CHS1101 net
after U9/R23 and the ADC path are drawn as connected, yet the near-zero test-point
value conflicts with the approximately 1.28 V follower/ADC readings. Possible
explanations include a differently identified point, local ground, observation
time or an as-built connectivity difference. None is proven. No net was declared
repaired and no new user measurement is required by this delivery.

The historical source also lacks a verified capture IRQ/callback sample-production
path and has a timer-period/overflow conflict. A multimeter average cannot prove
frequency, high time, duty cycle, edge amplitude or timer capture validity.
REAL humidity conversion/accuracy remains **PARTIAL / NOT VALIDATED**;
PWM waveform is **NOT TESTED**. SIM humidity in mixed mode is synthetic and
provides no evidence of real humidity accuracy. No humidity calibration was added.

## Original 2026-10-18 session and USB evidence

The specified original recording lasted 3917.513 host seconds (08:52:13.795 to
09:57:31.308 Asia/Shanghai). It has 11,571 raw/CSV records, 3790 TH3 frames,
82 events and 14 recorder segments. Sources are 3770 REAL/SIM frames and 20
REAL/REAL frames; those 20 retain invalid real humidity. Median forward sample
interval is 1015 ms; the largest forward interval is 12,180 ms. Four backward
counter jumps define separate MCU epochs, so final-counter minus first-counter
is not the session duration. Ten anomalous byte records are retained.

Nine disconnections/reopens and nine confirmed restores are recorded. Exact
raw-byte/CSV/metadata consistency and clean End Session pass. Missing originals
and diagnostic completeness are reported separately. The
[reviewed validation data](../release/VALIDATION.md)
contains counts, hashes and recovery timings; original files remain unchanged.

`[document claim]` USB was normally 5.02-5.07 V and occasionally approximately
4.0-4.45 V; AMS1117/STM32/ADS rails were reported as 3.29/3.28/3.29 V, TLC556 and
OPA330 supply about 5.07 V. Movement was associated with symptoms by the user.
`[inference]` Counter rollback supports a restart interpretation. Neither that
counter nor a Qt error proves a connector, cable, power, driver or solder-joint
root cause. There is no simultaneous independent power trace or enumeration
capture. Physical USB cause remains **UNRESOLVED**; no hardware fix is claimed.

## Final hardware disposition

| Capability | Disposition and boundary |
|---|---|
| Temperature acquisition/UART/Qt | Functional evidence available; current automatic gates are indexed in the experiment |
| Ten-pair temperature comparison | Recomputed; MAE/RMSE improved in this supplied sample only |
| Independent full-range temperature accuracy | NOT VALIDATED |
| Humidity divider/follower/ADC DC consistency | PARTIAL, user measurements plus design calculation |
| REAL humidity response/RH accuracy | PARTIAL / NOT VALIDATED; accepted final limitation |
| PWM waveform/frequency/duty | NOT TESTED; no oscilloscope |
| V_CHS1101 point/net identity | CONFLICT remains |
| USB movement/power physical root cause | UNRESOLVED |
| New optical OLED/cold-power measurement | NOT TESTED by agent; prior human observations retain their original scope |
| Another physical Windows PC | NOT TESTED; explicitly outside final scope |

## Course-report use

Use the implemented architecture, ADS units, explicit source modes, shared
OLED/protocol snapshot, recorder behavior, ten-pair table and final release/test
matrix as traceable material. Cite each observation as code, design, supplied
measurement or automated runtime evidence. Preserve negative results and the
humidity/USB limitations. Future work may improve as-built evidence and controlled
reference validation; it is not an unfinished calibration or PCB redesign task
in this authorized final delivery.
