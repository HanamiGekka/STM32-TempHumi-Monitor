# Architecture

STM32F103C8T6 acquires raw sensor inputs and publishes versioned measurements; Qt displays, records and analyzes the supplied semantics.

| Layer | Location | Responsibility |
|---|---|---|
| Platform | Core generated initialization, Drivers | Clock/GPIO/peripheral initialization and HAL/CMSIS |
| BSP | Core/Src/bsp_* | Pins, buses, low-level UART/I2C |
| Driver | Core/Src/drv_* | ADS1115, OLED and humidity PWM device/capture status |
| Algorithm | Core/Src/algo_sensor_convert.c | Pure NTC/humidity conversion with domain checks |
| Application | Core/Src/app_* | Scheduling, raw validity, REAL/SIM selection and hybrid window boundaries |
| Protocol | Core/Src/app_protocol.c | TH3 and shared measurement/error semantics |
| Desktop | UIDesktop | Serial/file/replay transport, parser/timeline, plots, recorder and observed analysis |

Higher layers consume lower layers. Algorithms do not call HAL; drivers do not decide UI wording; Qt does not recalibrate MCU values. Firmware source version is independent of desktop VERSION and flashed-image identity.

REAL/REAL is the firmware boot mode. Explicit source requests apply after the old sampling window publishes. TH3 frames carry both sources and generation; SIM physical fields are NA. A failing REAL channel remains REAL with an error. Desktop confirmation combines ACK, MODE readback and matching frames.

The main loop handles sampling and TX. UART RX ISR only queues/rearms bytes; bounded main-loop command parsing manages framing/errors. Generated code changes must remain inside protected user sections and be reviewed after CubeMX regeneration. Current pin/timer configuration is a code claim until physical connectivity/signal capture is measured.

See [protocol](../protocol/TH2_TH3.md), [hardware](../hardware/MEASUREMENT_GUIDE.md) and [desktop](../development/DESKTOP.md).
