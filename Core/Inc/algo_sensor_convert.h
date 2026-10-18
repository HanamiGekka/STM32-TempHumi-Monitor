#ifndef ALGO_SENSOR_CONVERT_H
#define ALGO_SENSOR_CONVERT_H

#include <stdbool.h>
#include <stdint.h>

/* ID 1 is a provisional temperature-only empirical adjustment, not accuracy
 * certification. The physical NTC and humidity conversions remain unchanged. */
#ifndef ALGO_TEMPERATURE_CORRECTION_ENABLED
#define ALGO_TEMPERATURE_CORRECTION_ENABLED 1
#endif
#ifdef __cplusplus
extern "C" {
#endif

float algo_temperature_correct_mean(float physical_mean_deg_c, bool enabled,
                                    uint16_t *correction_id);
/* Profiles: 0 off, 1 ambient offset, 2 guarded two-segment LAD,
 * 3 guarded six-segment Huber. All are provisional, not accuracy certificates. */
float algo_temperature_correct_profile(float physical_mean_deg_c, uint16_t profile,
                                      uint16_t *correction_id);

/*
 * 算法层不依赖 HAL 句柄，只负责把中间量换算成温度与湿度。
 * 当前公式参数依据 `references/原理图_温湿度传感器公式总结.md` 的阶段性整理；
 * 现阶段可用于软件实现与课程说明，但不是最终实机标定结论。
 */
typedef enum
{
  ALGO_SENSOR_DIAG_OK = 0,
  ALGO_SENSOR_DIAG_INPUT_NAN,
  ALGO_SENSOR_DIAG_INPUT_TOO_LOW,
  ALGO_SENSOR_DIAG_INPUT_TOO_HIGH,
  ALGO_SENSOR_DIAG_DENOM_TOO_SMALL,
  ALGO_SENSOR_DIAG_RESISTANCE_INVALID,
  ALGO_SENSOR_DIAG_LOG_INVALID,
  ALGO_SENSOR_DIAG_OUTPUT_OUT_OF_RANGE,
  ALGO_SENSOR_DIAG_CLAMPED_LOW,
  ALGO_SENSOR_DIAG_CLAMPED_HIGH
} algo_sensor_diag_reason_t;

typedef struct
{
  float v_adc;
  float r_ntc;
  float temp_c;
  algo_sensor_diag_reason_t reason;
} algo_ntc_diag_t;

typedef struct
{
  float v_adc;
  float rh_raw;
  float rh_clamped;
  algo_sensor_diag_reason_t reason;
} algo_rh_adc_diag_t;

float algo_ntc_voltage_to_temp_c(float v_adc);
float algo_ntc_voltage_to_temp_c_diag(float v_adc, algo_ntc_diag_t *diag);
float algo_humidity_duty_to_rh(float duty);
float algo_humidity_adc_to_rh(float v_adc);
float algo_humidity_adc_to_rh_diag(float v_adc, algo_rh_adc_diag_t *diag);
const char *algo_sensor_diag_reason_to_str(algo_sensor_diag_reason_t reason);
float algo_ntc_expected_voltage_from_temp_c(float temp_c);
float algo_humidity_expected_voltage_from_rh(float rh);

#ifdef __cplusplus
}
#endif

#endif /* ALGO_SENSOR_CONVERT_H */
