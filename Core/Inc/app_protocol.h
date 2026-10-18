#ifndef APP_PROTOCOL_H
#define APP_PROTOCOL_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#include "app_measurement_source.h"

/*
 * 协议模块只负责把业务信息格式化为稳定文本，不负责串口发送。
 * 当前阶段的文本角色分工固定为：
 * - `BOOT OK` / `[SCAN_SUMMARY]` / `[STATUS]` / `[SAMPLE_CFG]`：启动 bring-up 观察入口；
 * - `[ADS_DBG]` / `[ADS_STATE]` / `[MEASURE]`：给人工看 ADS 单次转换诊断与完整中间量；
 * - `TH,...`：给上位机脚本或串口工具做稳定 CSV 解析。
 */

int app_protocol_format_boot(char *buffer, uint32_t buffer_size, const char *boot_text);
int app_protocol_format_scan_summary(char *buffer, uint32_t buffer_size, const char *bus_name, uint8_t device_count);
/* [STATUS] 主要用于启动 bring-up，描述 OLED / ADS1115 / PWM 当前准备状态。 */
int app_protocol_format_status_summary(char *buffer,
                                       uint32_t buffer_size,
                                       uint8_t oled_ready,
                                       uint8_t ads_ready,
                                       uint8_t pwm_initialized,
                                       uint8_t pwm_started,
                                       uint8_t pwm_has_valid_sample);
/* [SAMPLE_CFG] 固化当前窗口时长、组数和通道数，避免文档与代码口径分离。 */
int app_protocol_format_sampling_summary(char *buffer,
                                         uint32_t buffer_size,
                                         uint16_t sample_groups_per_second,
                                         uint16_t sample_channel_count,
                                         uint16_t publish_window_ms);
/* [MEASURE] 面向人工读日志；TH CSV 面向上位机或后续脚本解析。 */
int app_protocol_format_measure_log(char *buffer,
                                    uint32_t buffer_size,
                                    float v_ntc,
                                    float v_chs1101,
                                    float temp_c,
                                    float rh_pwm,
                                    float rh_adc);
int app_protocol_format_ads_debug(char *buffer,
                                  uint32_t buffer_size,
                                  uint8_t ch0_ok,
                                  int16_t raw0,
                                  float v0,
                                  uint8_t ch1_ok,
                                  int16_t raw1,
                                  float v1);
int app_protocol_format_ads_state(char *buffer,
                                  uint32_t buffer_size,
                                  uint8_t ads_ready,
                                  const char *ch0_state,
                                  uint8_t ch0_hal_status,
                                  uint16_t ch0_error_count,
                                  const char *ch0_last_fail,
                                  const char *ch1_state,
                                  uint8_t ch1_hal_status,
                                  uint16_t ch1_error_count,
                                  const char *ch1_last_fail);
int app_protocol_format_ntc_debug(char *buffer,
                                  uint32_t buffer_size,
                                  float v_adc,
                                  float r_ntc,
                                  float temp_c,
                                  const char *state);
int app_protocol_format_rh_adc_debug(char *buffer,
                                     uint32_t buffer_size,
                                     float v_adc,
                                     float rh_raw,
                                     float rh_clamped,
                                     const char *state);
int app_protocol_format_pwm_debug(char *buffer,
                                  uint32_t buffer_size,
                                  uint8_t initialized,
                                  uint8_t started,
                                  uint8_t has_valid_sample,
                                  uint32_t period_ticks,
                                  uint32_t high_ticks,
                                  float duty);
int app_protocol_format_ref_expect(char *buffer, uint32_t buffer_size);
/* Additive read-only identity response. UID bytes preserve memory order. */
int app_protocol_format_identity(char *buffer,uint32_t size,const uint8_t uid[12],uint16_t flash_kib,uint16_t device_id);
/* Human diagnostic only; preserves exact uncorrected mean float bits. */
int app_protocol_format_temperature_correction(char *buffer, uint32_t buffer_size,
                                                float raw_mean, float corrected_mean,
                                                uint16_t calibration_id);
int app_protocol_format_temperature_evidence(char *buffer, uint32_t buffer_size,
                                            float raw_mean, float corrected_mean,
                                            uint16_t calibration_id, uint32_t timestamp_ms,
                                            uint16_t requested_profile);
/* Rendering/transport evidence, not optical verification or sensor accuracy. */
int app_protocol_format_oled_temperature(char *buffer, uint32_t buffer_size,
                                         uint32_t timestamp_ms, float temperature_c,
                                         app_measurement_source_type_t temperature_source,
                                         uint16_t profile, uint16_t calibration_id,
                                         const char *temperature_text,
                                         uint8_t temperature_shown, uint8_t hal_status);
int app_protocol_format_source_summary(char *buffer, uint32_t buffer_size,
                                       app_measurement_source_type_t source_type);
/* TH3 is the only active machine frame; see docs/protocol/TH2_TH3.md. */
int app_protocol_format_measurement_v3(char *buffer,uint32_t buffer_size,const measurement_data_t *data,
  app_measurement_source_type_t temperature,app_measurement_source_type_t humidity,uint32_t generation);

#ifdef __cplusplus
}
#endif

#endif /* APP_PROTOCOL_H */
