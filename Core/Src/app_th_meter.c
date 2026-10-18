#include "app_th_meter.h"

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

#include "algo_sensor_convert.h"
#include "app_measurement_source_sim.h"
#include "app_protocol.h"
#include "app_version.h"
#include "bsp_i2c_scan.h"
#include "bsp_led_key.h"
#include "bsp_uart_log.h"
#include "drv_ads1115.h"
#include "drv_humidity_pwm.h"
#include "drv_oled.h"

extern I2C_HandleTypeDef hi2c1;
extern I2C_HandleTypeDef hi2c2;
extern UART_HandleTypeDef huart1;

/*
 * 这些常量定义的是当前可信软件基线，不是任意可放大的理论目标值。
 * 参数来源分两类：
 * 1. `500 ms`、`200 ms`、`1000 ms` 来自当前应用层调度默认实现；
 * 2. `16 groups/s`、`16..64`、`3 channels` 来自当前窗口采样策略。
 *
 * 其中：
 * - `16 groups/s` 是结合“每组 2 次 ADS1115 读取 + 1 次 PWM 查询”后选出的保守默认值；
 * - `16..64` 只是编译期允许范围，当前只有默认 `16` 视为可信结论；
 * - `3 channels` 指每组固定尝试 `V_NTC / RH_PWM / V_CHS1101` 三项输入。
 */
#define APP_TH_METER_LED_PERIOD_MS                 500U
#define APP_TH_METER_KEY_REPORT_PERIOD_MS          200U
#define APP_TH_METER_SAMPLE_PUBLISH_PERIOD_MS      1000U
#define APP_TH_METER_SAMPLE_GROUPS_MIN             16U
#define APP_TH_METER_SAMPLE_GROUPS_MAX             64U
#define APP_TH_METER_SAMPLE_GROUPS_PER_SECOND      16U
#define APP_TH_METER_SAMPLE_CHANNEL_COUNT          3U
/* 预算器以“每秒 1000 ms”为分母做整数累积，避免为采样调度引入浮点。 */
#define APP_TH_METER_SAMPLE_BUDGET_UNIT            1000U
#define APP_TH_METER_LOG_BUFFER_SIZE               224U
#define APP_TH_METER_OLED_PAGE_COUNT               3U
#define APP_TH_METER_ENABLE_ADS_DEBUG              1U
#define APP_TH_METER_ENABLE_BRINGUP_DIAG           1U
#define APP_TH_METER_STARTUP_LOG_DELAY_MS          500U
#define APP_TH_METER_STARTUP_REPEAT_COUNT          2U
#define APP_TH_METER_ENABLE_OLED_DIAG_PAGE         1U
#define APP_TH_METER_ENABLE_SENSOR_DIAG            1U
#define APP_TH_METER_ENABLE_REF_EXPECT             1U

#define APP_TH_METER_TEMP_CHANNEL                  DRV_ADS1115_CHANNEL_V_NTC
#define APP_TH_METER_HUMIDITY_ADC_CHANNEL          DRV_ADS1115_CHANNEL_V_CHS1101

#if (APP_TH_METER_SAMPLE_GROUPS_PER_SECOND < APP_TH_METER_SAMPLE_GROUPS_MIN) || \
    (APP_TH_METER_SAMPLE_GROUPS_PER_SECOND > APP_TH_METER_SAMPLE_GROUPS_MAX)
#error "APP_TH_METER_SAMPLE_GROUPS_PER_SECOND must stay within 16..64 groups per second."
#endif

typedef measurement_data_t app_th_meter_sample_t;

typedef enum
{
  APP_TH_METER_ADS_CH_SKIP = 0,
  APP_TH_METER_ADS_CH_OK,
  APP_TH_METER_ADS_CH_ERROR,
  APP_TH_METER_ADS_CH_TIMEOUT
} app_th_meter_ads_channel_state_t;

typedef struct
{
  uint8_t ch0_ok;
  int16_t raw0;
  float v0;
  uint8_t ch1_ok;
  int16_t raw1;
  float v1;
  uint8_t ch0_hal_status;
  uint8_t ch1_hal_status;
  uint16_t ch0_error_count;
  uint16_t ch1_error_count;
  drv_ads1115_diag_reason_t ch0_last_fail;
  drv_ads1115_diag_reason_t ch1_last_fail;
  app_th_meter_ads_channel_state_t ch0_state;
  app_th_meter_ads_channel_state_t ch1_state;
} app_th_meter_ads_debug_t;

typedef struct
{
  /* 按字段分别累计有效值，允许某一条链暂时无效而不拖垮整窗发布。 */
  float v_ntc_sum;
  float v_chs1101_sum;
  float temp_c_sum;
  float humidity_rh_sum;
  float rh_pwm_sum;
  float rh_adc_sum;
  uint16_t v_ntc_count;
  uint16_t v_chs1101_count;
  uint16_t temp_c_count;
  uint16_t humidity_rh_count;
  uint16_t rh_pwm_count;
  uint16_t rh_adc_count;
  uint16_t group_count;
  uint16_t common_valid_flags;
  uint16_t error_flags;
  int16_t latest_raw_ntc_code;
  int16_t latest_raw_chs1101_code;
  uint32_t latest_pwm_period_ticks;
  uint32_t latest_pwm_high_ticks;
} app_th_meter_sample_accumulator_t;

typedef struct
{
  bool initialized;
  bool oled_ready;
  bool ads1115_ready;
  bool humidity_pwm_initialized;
  bool humidity_pwm_started;
  uint8_t i2c1_device_count;
  uint8_t i2c2_device_count;
  uint32_t last_led_tick;
  uint32_t last_key_tick;
  uint32_t last_sample_scheduler_tick;
  uint32_t last_sample_publish_tick;
  uint32_t sample_group_budget;
  uint8_t oled_page_index;
  app_th_meter_sample_accumulator_t sample_accumulator;
  app_th_meter_ads_debug_t latest_ads_debug;
  const app_measurement_source_t *source;
} app_th_meter_state_t;

static app_th_meter_state_t s_app_th_meter = {0};
static app_measurement_source_type_t s_requested_source_type = APP_MEASUREMENT_SOURCE_REAL;
static app_hybrid_mode_t s_hybrid_mode;
#ifndef APP_ENABLE_REMOTE_SIMULATION
#define APP_ENABLE_REMOTE_SIMULATION 1
#endif

static void app_th_meter_real_source_read(measurement_data_t *out_data, uint32_t timestamp_ms);

static const app_measurement_source_t s_app_th_meter_real_source =
{
  APP_MEASUREMENT_SOURCE_REAL,
  NULL,
  app_th_meter_real_source_read
};

static void app_th_meter_write_text(const char *text)
{
  if (text != NULL)
  {
    (void)bsp_uart_log_write(text);
  }
}

static void app_th_meter_write_protocol_line(int format_length, const char *buffer)
{
  if ((format_length > 0) && (buffer != NULL))
  {
    (void)bsp_uart_log_write(buffer);
  }
}

static void app_th_meter_write_startup_protocol_line(int format_length, const char *buffer)
{
#if (APP_TH_METER_ENABLE_BRINGUP_DIAG != 0U)
  uint8_t repeat_index = 0U;

  if ((format_length <= 0) || (buffer == NULL))
  {
    return;
  }

  for (repeat_index = 0U; repeat_index < APP_TH_METER_STARTUP_REPEAT_COUNT; repeat_index++)
  {
    (void)bsp_uart_log_write(buffer);
    HAL_Delay(APP_TH_METER_STARTUP_LOG_DELAY_MS);
  }
#else
  app_th_meter_write_protocol_line(format_length, buffer);
#endif
}

static void app_th_meter_sample_reset(app_th_meter_sample_t *sample)
{
  if (sample == NULL)
  {
    return;
  }

  app_measurement_source_reset_data(sample, s_requested_source_type, 0U);
}

static void app_th_meter_ads_debug_reset(app_th_meter_ads_debug_t *ads_debug)
{
  if (ads_debug == NULL)
  {
    return;
  }

  ads_debug->ch0_ok = 0U;
  ads_debug->raw0 = 0;
  ads_debug->v0 = NAN;
  ads_debug->ch1_ok = 0U;
  ads_debug->raw1 = 0;
  ads_debug->v1 = NAN;
  ads_debug->ch0_hal_status = (uint8_t)HAL_BUSY;
  ads_debug->ch1_hal_status = (uint8_t)HAL_BUSY;
  ads_debug->ch0_error_count = 0U;
  ads_debug->ch1_error_count = 0U;
  ads_debug->ch0_last_fail = DRV_ADS1115_DIAG_NO_ADS;
  ads_debug->ch1_last_fail = DRV_ADS1115_DIAG_NO_ADS;
  ads_debug->ch0_state = APP_TH_METER_ADS_CH_SKIP;
  ads_debug->ch1_state = APP_TH_METER_ADS_CH_SKIP;
}

static void app_th_meter_ads_debug_prepare_sample(app_th_meter_ads_debug_t *ads_debug)
{
  if (ads_debug == NULL)
  {
    return;
  }

  ads_debug->ch0_ok = 0U;
  ads_debug->raw0 = 0;
  ads_debug->v0 = NAN;
  ads_debug->ch1_ok = 0U;
  ads_debug->raw1 = 0;
  ads_debug->v1 = NAN;
  ads_debug->ch0_hal_status = (uint8_t)HAL_BUSY;
  ads_debug->ch1_hal_status = (uint8_t)HAL_BUSY;
  ads_debug->ch0_state = APP_TH_METER_ADS_CH_SKIP;
  ads_debug->ch1_state = APP_TH_METER_ADS_CH_SKIP;
}

static bool app_th_meter_ads_is_not_ready(void)
{
  return ((s_app_th_meter.i2c2_device_count == 0U) || (!s_app_th_meter.ads1115_ready));
}

static const char *app_th_meter_ads_channel_state_text(app_th_meter_ads_channel_state_t state)
{
  switch (state)
  {
    case APP_TH_METER_ADS_CH_SKIP:
      return "SKIP";

    case APP_TH_METER_ADS_CH_OK:
      return "OK";

    case APP_TH_METER_ADS_CH_ERROR:
      return "ERROR";

    case APP_TH_METER_ADS_CH_TIMEOUT:
      return "TIMEOUT";

    default:
      return "ERROR";
  }
}

static uint16_t app_th_meter_saturating_inc_u16(uint16_t value)
{
  if (value == 0xFFFFU)
  {
    return value;
  }

  return (uint16_t)(value + 1U);
}

static const char *app_th_meter_ads_diag_reason_text(drv_ads1115_diag_reason_t reason)
{
  switch (reason)
  {
    case DRV_ADS1115_DIAG_NONE:
      return "none";

    case DRV_ADS1115_DIAG_NOT_INITIALIZED:
      return "not_init";

    case DRV_ADS1115_DIAG_NULL_ARG:
      return "null_arg";

    case DRV_ADS1115_DIAG_INVALID_CHANNEL:
      return "bad_ch";

    case DRV_ADS1115_DIAG_NO_ADS:
      return "no_ads";

    case DRV_ADS1115_DIAG_PROBE_FAIL:
      return "probe";

    case DRV_ADS1115_DIAG_CONFIG_WRITE_FAIL:
      return "cfg_write";

    case DRV_ADS1115_DIAG_CONFIG_READ_FAIL:
      return "cfg_read";

    case DRV_ADS1115_DIAG_OS_TIMEOUT:
      return "os_timeout";

    case DRV_ADS1115_DIAG_CONVERSION_READ_FAIL:
      return "conv_read";

    default:
      return "unknown";
  }
}

static const char *app_th_meter_sensor_reason_short(algo_sensor_diag_reason_t reason)
{
  switch (reason)
  {
    case ALGO_SENSOR_DIAG_OK:
      return "OK";

    case ALGO_SENSOR_DIAG_OUTPUT_OUT_OF_RANGE:
      return "OOR";

    case ALGO_SENSOR_DIAG_CLAMPED_LOW:
      return "LOW";

    case ALGO_SENSOR_DIAG_INPUT_NAN:
      return "SKIP";

    case ALGO_SENSOR_DIAG_INPUT_TOO_LOW:
      return "LOW";

    case ALGO_SENSOR_DIAG_INPUT_TOO_HIGH:
    case ALGO_SENSOR_DIAG_DENOM_TOO_SMALL:
    case ALGO_SENSOR_DIAG_RESISTANCE_INVALID:
    case ALGO_SENSOR_DIAG_LOG_INVALID:
    case ALGO_SENSOR_DIAG_CLAMPED_HIGH:
      return "OOR";

    default:
      return "SKIP";
  }
}

static const char *app_th_meter_ntc_state_short(const app_th_meter_sample_t *sample)
{
  algo_ntc_diag_t ntc_diag;

  if (app_th_meter_ads_is_not_ready())
  {
    return "NOADS";
  }

  if ((sample == NULL) || (s_app_th_meter.latest_ads_debug.ch0_state != APP_TH_METER_ADS_CH_OK))
  {
    return "SKIP";
  }

  (void)algo_ntc_voltage_to_temp_c_diag(sample->v_ntc, &ntc_diag);
  return app_th_meter_sensor_reason_short(ntc_diag.reason);
}

static const char *app_th_meter_rh_adc_state_short(const app_th_meter_sample_t *sample)
{
  algo_rh_adc_diag_t rh_adc_diag;

  if (app_th_meter_ads_is_not_ready())
  {
    return "NOADS";
  }

  if ((sample == NULL) || (s_app_th_meter.latest_ads_debug.ch1_state != APP_TH_METER_ADS_CH_OK))
  {
    return "SKIP";
  }

  (void)algo_humidity_adc_to_rh_diag(sample->v_chs1101, &rh_adc_diag);
  return app_th_meter_sensor_reason_short(rh_adc_diag.reason);
}

static const char *app_th_meter_pwm_state_short(const drv_humidity_pwm_status_t *pwm_status)
{
  if ((pwm_status == NULL) || (!pwm_status->initialized) || (!pwm_status->started))
  {
    return "SKIP";
  }

  return pwm_status->has_valid_sample ? "OK" : "WAIT";
}

static void app_th_meter_sample_accumulator_reset(app_th_meter_sample_accumulator_t *accumulator)
{
  if (accumulator == NULL)
  {
    return;
  }

  *accumulator = (app_th_meter_sample_accumulator_t){0};
}

static void app_th_meter_sample_accumulator_add_value(float value, float *sum, uint16_t *count)
{
  /* 当前统一把无效值保留为 NAN，累加器只统计真正可用的字段。 */
  if ((sum == NULL) || (count == NULL) || (!isfinite(value)))
  {
    return;
  }

  *sum += value;
  (*count)++;
}

static void app_th_meter_sample_accumulator_add(app_th_meter_sample_accumulator_t *accumulator,
                                                const app_th_meter_sample_t *sample)
{
  if ((accumulator == NULL) || (sample == NULL))
  {
    return;
  }

  accumulator->group_count++;

  app_th_meter_sample_accumulator_add_value(sample->v_ntc, &accumulator->v_ntc_sum, &accumulator->v_ntc_count);
  app_th_meter_sample_accumulator_add_value(sample->v_chs1101, &accumulator->v_chs1101_sum, &accumulator->v_chs1101_count);
  app_th_meter_sample_accumulator_add_value(sample->temperature_c, &accumulator->temp_c_sum, &accumulator->temp_c_count);
  app_th_meter_sample_accumulator_add_value(sample->humidity_rh, &accumulator->humidity_rh_sum, &accumulator->humidity_rh_count);
  app_th_meter_sample_accumulator_add_value(sample->rh_pwm, &accumulator->rh_pwm_sum, &accumulator->rh_pwm_count);
  app_th_meter_sample_accumulator_add_value(sample->rh_adc, &accumulator->rh_adc_sum, &accumulator->rh_adc_count);
  if (accumulator->group_count == 1U)
  {
    accumulator->common_valid_flags = sample->valid_flags;
  }
  else
  {
    accumulator->common_valid_flags &= sample->valid_flags;
  }
  accumulator->error_flags |= sample->error_flags;
  accumulator->latest_raw_ntc_code = sample->raw_ntc_code;
  accumulator->latest_raw_chs1101_code = sample->raw_chs1101_code;
  accumulator->latest_pwm_period_ticks = sample->pwm_period_ticks;
  accumulator->latest_pwm_high_ticks = sample->pwm_high_ticks;
}

static float app_th_meter_sample_accumulator_average(float sum, uint16_t count)
{
  if (count == 0U)
  {
    return NAN;
  }

  return sum / (float)count;
}

static void app_th_meter_sample_accumulator_finalize(const app_th_meter_sample_accumulator_t *accumulator,
                                                     app_th_meter_sample_t *sample)
{
  if (sample == NULL)
  {
    return;
  }

  app_th_meter_sample_reset(sample);
  if (accumulator == NULL)
  {
    return;
  }

  sample->v_ntc = app_th_meter_sample_accumulator_average(accumulator->v_ntc_sum, accumulator->v_ntc_count);
  sample->v_chs1101 = app_th_meter_sample_accumulator_average(accumulator->v_chs1101_sum, accumulator->v_chs1101_count);
  sample->temperature_c = app_th_meter_sample_accumulator_average(accumulator->temp_c_sum, accumulator->temp_c_count);
  sample->humidity_rh = app_th_meter_sample_accumulator_average(accumulator->humidity_rh_sum, accumulator->humidity_rh_count);
  sample->rh_pwm = app_th_meter_sample_accumulator_average(accumulator->rh_pwm_sum, accumulator->rh_pwm_count);
  sample->rh_adc = app_th_meter_sample_accumulator_average(accumulator->rh_adc_sum, accumulator->rh_adc_count);
  sample->valid_flags = accumulator->common_valid_flags;
  sample->error_flags = accumulator->error_flags;
  sample->raw_ntc_code = accumulator->latest_raw_ntc_code;
  sample->raw_chs1101_code = accumulator->latest_raw_chs1101_code;
  sample->pwm_period_ticks = accumulator->latest_pwm_period_ticks;
  sample->pwm_high_ticks = accumulator->latest_pwm_high_ticks;
  sample->sample_count = accumulator->group_count;
  sample->valid = ((sample->valid_flags & (APP_MEASUREMENT_VALID_TEMPERATURE | APP_MEASUREMENT_VALID_HUMIDITY)) ==
                   (APP_MEASUREMENT_VALID_TEMPERATURE | APP_MEASUREMENT_VALID_HUMIDITY));
}

static void app_th_meter_log_boot(void)
{
  char boot_text[64] = {0};
  char buffer[APP_TH_METER_LOG_BUFFER_SIZE] = {0};
  int format_length = 0;

  (void)snprintf(boot_text, sizeof(boot_text), "BOOT OK %s", APP_VERSION_FULL_STR);
  format_length = app_protocol_format_boot(buffer, sizeof(buffer), boot_text);
  app_th_meter_write_startup_protocol_line(format_length, buffer);
}

static void app_th_meter_log_source(void)
{
  char buffer[APP_TH_METER_LOG_BUFFER_SIZE] = {0};
  int format_length = app_protocol_format_source_summary(buffer, sizeof(buffer),
                                                         s_app_th_meter.source->type);
  app_th_meter_write_startup_protocol_line(format_length, buffer);
}

static void app_th_meter_log_scan_summary(const char *bus_name, uint8_t device_count)
{
  char buffer[APP_TH_METER_LOG_BUFFER_SIZE] = {0};
  int format_length = 0;

  format_length = app_protocol_format_scan_summary(buffer, sizeof(buffer), bus_name, device_count);
  app_th_meter_write_startup_protocol_line(format_length, buffer);
}

static void app_th_meter_log_status_summary(void)
{
  drv_humidity_pwm_status_t pwm_status = {0};
  char buffer[APP_TH_METER_LOG_BUFFER_SIZE] = {0};
  int format_length = 0;

  (void)drv_humidity_pwm_get_status(&pwm_status);

  format_length = app_protocol_format_status_summary(buffer,
                                                     sizeof(buffer),
                                                     s_app_th_meter.oled_ready ? 1U : 0U,
                                                     s_app_th_meter.ads1115_ready ? 1U : 0U,
                                                     pwm_status.initialized ? 1U : 0U,
                                                     pwm_status.started ? 1U : 0U,
                                                     pwm_status.has_valid_sample ? 1U : 0U);
  app_th_meter_write_startup_protocol_line(format_length, buffer);
}

static void app_th_meter_log_sampling_summary(void)
{
  char buffer[APP_TH_METER_LOG_BUFFER_SIZE] = {0};
  int format_length = 0;

  /* 把窗口时长、组数和通道数直接打印出来，避免文档与代码口径分离。 */
  format_length = app_protocol_format_sampling_summary(buffer,
                                                       sizeof(buffer),
                                                       APP_TH_METER_SAMPLE_GROUPS_PER_SECOND,
                                                       APP_TH_METER_SAMPLE_CHANNEL_COUNT,
                                                       APP_TH_METER_SAMPLE_PUBLISH_PERIOD_MS);
  app_th_meter_write_startup_protocol_line(format_length, buffer);
}

static void app_th_meter_log_ref_expect(void)
{
#if (APP_TH_METER_ENABLE_REF_EXPECT != 0U)
  char buffer[APP_TH_METER_LOG_BUFFER_SIZE] = {0};
  int format_length = 0;

  format_length = app_protocol_format_ref_expect(buffer, sizeof(buffer));
  app_th_meter_write_startup_protocol_line(format_length, buffer);
#endif
}

static void app_th_meter_log_ads_debug(void)
{
#if (APP_TH_METER_ENABLE_ADS_DEBUG != 0U)
  char buffer[APP_TH_METER_LOG_BUFFER_SIZE] = {0};
  int format_length = 0;
  const app_th_meter_ads_debug_t *ads_debug = &s_app_th_meter.latest_ads_debug;

  format_length = app_protocol_format_ads_debug(buffer,
                                                sizeof(buffer),
                                                ads_debug->ch0_ok,
                                                ads_debug->raw0,
                                                ads_debug->v0,
                                                ads_debug->ch1_ok,
                                                ads_debug->raw1,
                                                ads_debug->v1);
  app_th_meter_write_protocol_line(format_length, buffer);
#endif
}

static void app_th_meter_log_ads_state(void)
{
#if (APP_TH_METER_ENABLE_ADS_DEBUG != 0U)
  char buffer[APP_TH_METER_LOG_BUFFER_SIZE] = {0};
  int format_length = 0;
  const app_th_meter_ads_debug_t *ads_debug = &s_app_th_meter.latest_ads_debug;

  format_length = app_protocol_format_ads_state(buffer,
                                                sizeof(buffer),
                                                app_th_meter_ads_is_not_ready() ? 0U : 1U,
                                                app_th_meter_ads_channel_state_text(ads_debug->ch0_state),
                                                ads_debug->ch0_hal_status,
                                                ads_debug->ch0_error_count,
                                                app_th_meter_ads_diag_reason_text(ads_debug->ch0_last_fail),
                                                app_th_meter_ads_channel_state_text(ads_debug->ch1_state),
                                                ads_debug->ch1_hal_status,
                                                ads_debug->ch1_error_count,
                                                app_th_meter_ads_diag_reason_text(ads_debug->ch1_last_fail));
  app_th_meter_write_protocol_line(format_length, buffer);
#endif
}

static void app_th_meter_log_sensor_diag(const app_th_meter_sample_t *sample)
{
#if (APP_TH_METER_ENABLE_SENSOR_DIAG != 0U)
  algo_ntc_diag_t ntc_diag;
  algo_rh_adc_diag_t rh_adc_diag;
  drv_humidity_pwm_status_t pwm_status = {0};
  char buffer[APP_TH_METER_LOG_BUFFER_SIZE] = {0};
  int format_length = 0;

  if (sample == NULL)
  {
    return;
  }

  (void)algo_ntc_voltage_to_temp_c_diag(sample->v_ntc, &ntc_diag);
  format_length = app_protocol_format_ntc_debug(buffer,
                                                sizeof(buffer),
                                                ntc_diag.v_adc,
                                                ntc_diag.r_ntc,
                                                ntc_diag.temp_c,
                                                algo_sensor_diag_reason_to_str(ntc_diag.reason));
  app_th_meter_write_protocol_line(format_length, buffer);

  (void)algo_humidity_adc_to_rh_diag(sample->v_chs1101, &rh_adc_diag);
  format_length = app_protocol_format_rh_adc_debug(buffer,
                                                   sizeof(buffer),
                                                   rh_adc_diag.v_adc,
                                                   rh_adc_diag.rh_raw,
                                                   rh_adc_diag.rh_clamped,
                                                   algo_sensor_diag_reason_to_str(rh_adc_diag.reason));
  app_th_meter_write_protocol_line(format_length, buffer);

  (void)drv_humidity_pwm_get_status(&pwm_status);
  format_length = app_protocol_format_pwm_debug(buffer,
                                                sizeof(buffer),
                                                pwm_status.initialized ? 1U : 0U,
                                                pwm_status.started ? 1U : 0U,
                                                pwm_status.has_valid_sample ? 1U : 0U,
                                                pwm_status.last_period_ticks,
                                                pwm_status.last_high_ticks,
                                                pwm_status.last_duty);
  app_th_meter_write_protocol_line(format_length, buffer);
#endif
}

static void app_th_meter_record_ads_failure(app_th_meter_ads_debug_t *ads_debug,
                                            uint8_t channel_index,
                                            HAL_StatusTypeDef status,
                                            drv_ads1115_diag_reason_t reason)
{
  if (ads_debug == NULL)
  {
    return;
  }

  if (channel_index == 0U)
  {
    ads_debug->ch0_hal_status = (uint8_t)status;
    ads_debug->ch0_error_count = app_th_meter_saturating_inc_u16(ads_debug->ch0_error_count);
    ads_debug->ch0_last_fail = reason;
    ads_debug->ch0_state = ((status == HAL_TIMEOUT) || (reason == DRV_ADS1115_DIAG_OS_TIMEOUT)) ?
                           APP_TH_METER_ADS_CH_TIMEOUT :
                           APP_TH_METER_ADS_CH_ERROR;
  }
  else
  {
    ads_debug->ch1_hal_status = (uint8_t)status;
    ads_debug->ch1_error_count = app_th_meter_saturating_inc_u16(ads_debug->ch1_error_count);
    ads_debug->ch1_last_fail = reason;
    ads_debug->ch1_state = ((status == HAL_TIMEOUT) || (reason == DRV_ADS1115_DIAG_OS_TIMEOUT)) ?
                           APP_TH_METER_ADS_CH_TIMEOUT :
                           APP_TH_METER_ADS_CH_ERROR;
  }
}

static void app_th_meter_mark_ads_skipped(app_th_meter_ads_debug_t *ads_debug)
{
  if (ads_debug == NULL)
  {
    return;
  }

  ads_debug->ch0_hal_status = (uint8_t)HAL_BUSY;
  ads_debug->ch1_hal_status = (uint8_t)HAL_BUSY;
  ads_debug->ch0_last_fail = DRV_ADS1115_DIAG_NO_ADS;
  ads_debug->ch1_last_fail = DRV_ADS1115_DIAG_NO_ADS;
  ads_debug->ch0_state = APP_TH_METER_ADS_CH_SKIP;
  ads_debug->ch1_state = APP_TH_METER_ADS_CH_SKIP;
}

static void app_th_meter_read_ads1115_sample(app_th_meter_sample_t *sample)
{
  app_th_meter_ads_debug_t *ads_debug = &s_app_th_meter.latest_ads_debug;
  HAL_StatusTypeDef ch0_status = HAL_ERROR;
  HAL_StatusTypeDef ch1_status = HAL_ERROR;
  drv_ads1115_diag_reason_t ch0_reason = DRV_ADS1115_DIAG_NONE;
  drv_ads1115_diag_reason_t ch1_reason = DRV_ADS1115_DIAG_NONE;
  int16_t raw_ntc = 0;
  int16_t raw_chs1101 = 0;

  /* 当前一组样本里固定读取两路 ADS1115：温度链 AIN0 + 湿度模拟链 AIN1。 */
  app_th_meter_ads_debug_prepare_sample(ads_debug);

  if (sample == NULL)
  {
    return;
  }

  if (app_th_meter_ads_is_not_ready())
  {
    app_th_meter_mark_ads_skipped(ads_debug);
    return;
  }

  ch0_status = drv_ads1115_read_raw(APP_TH_METER_TEMP_CHANNEL, &raw_ntc);
  (void)drv_ads1115_get_last_diag(&ch0_reason);
  if (ch0_status == HAL_OK)
  {
    ads_debug->ch0_ok = 1U;
    ads_debug->ch0_hal_status = (uint8_t)HAL_OK;
    ads_debug->raw0 = raw_ntc;
    ads_debug->v0 = drv_ads1115_raw_to_voltage(raw_ntc);
    ads_debug->ch0_state = APP_TH_METER_ADS_CH_OK;
    ads_debug->ch0_last_fail = DRV_ADS1115_DIAG_NONE;
    sample->v_ntc = ads_debug->v0;
    sample->temperature_c = algo_ntc_voltage_to_temp_c(sample->v_ntc);
  }
  else
  {
    app_th_meter_record_ads_failure(ads_debug, 0U, ch0_status, ch0_reason);
  }

  ch1_status = drv_ads1115_read_raw(APP_TH_METER_HUMIDITY_ADC_CHANNEL, &raw_chs1101);
  (void)drv_ads1115_get_last_diag(&ch1_reason);
  if (ch1_status == HAL_OK)
  {
    ads_debug->ch1_ok = 1U;
    ads_debug->ch1_hal_status = (uint8_t)HAL_OK;
    ads_debug->raw1 = raw_chs1101;
    ads_debug->v1 = drv_ads1115_raw_to_voltage(raw_chs1101);
    ads_debug->ch1_state = APP_TH_METER_ADS_CH_OK;
    ads_debug->ch1_last_fail = DRV_ADS1115_DIAG_NONE;
    sample->v_chs1101 = ads_debug->v1;
    sample->rh_adc = algo_humidity_adc_to_rh(sample->v_chs1101);
  }
  else
  {
    app_th_meter_record_ads_failure(ads_debug, 1U, ch1_status, ch1_reason);
  }
}

static void app_th_meter_read_pwm_sample(app_th_meter_sample_t *sample)
{
  float duty = 0.0f;

  /* PWM 模块当前允许“已启动但仍无有效样本”，因此这里只在 HAL_OK 时更新 RH_PWM。 */
  if ((sample == NULL) || (!s_app_th_meter.humidity_pwm_started))
  {
    return;
  }

  if (drv_humidity_pwm_get_duty(&duty) == HAL_OK)
  {
    sample->rh_pwm = algo_humidity_duty_to_rh(duty);
  }
}

static void app_th_meter_real_source_read(measurement_data_t *out_data, uint32_t timestamp_ms)
{
  algo_ntc_diag_t ntc_diag;
  algo_rh_adc_diag_t rh_adc_diag;
  drv_humidity_pwm_status_t pwm_status = {0};

  if (out_data == NULL)
  {
    return;
  }

  out_data->timestamp_ms = timestamp_ms;
  app_th_meter_read_ads1115_sample(out_data);
  app_th_meter_read_pwm_sample(out_data);

  if (app_th_meter_ads_is_not_ready())
  {
    out_data->error_flags |= APP_MEASUREMENT_ERROR_ADS_NOT_READY;
  }
  if (s_app_th_meter.latest_ads_debug.ch0_state == APP_TH_METER_ADS_CH_OK)
  {
    out_data->raw_ntc_code = s_app_th_meter.latest_ads_debug.raw0;
    out_data->valid_flags |= APP_MEASUREMENT_VALID_RAW_NTC | APP_MEASUREMENT_VALID_V_NTC;
    (void)algo_ntc_voltage_to_temp_c_diag(out_data->v_ntc, &ntc_diag);
    if (ntc_diag.reason == ALGO_SENSOR_DIAG_OK)
    {
      out_data->valid_flags |= APP_MEASUREMENT_VALID_TEMPERATURE;
    }
    else
    {
      out_data->error_flags |= APP_MEASUREMENT_ERROR_NTC_RANGE;
    }
  }
  else
  {
    out_data->error_flags |= (s_app_th_meter.latest_ads_debug.ch0_state == APP_TH_METER_ADS_CH_TIMEOUT) ?
                             APP_MEASUREMENT_ERROR_NTC_TIMEOUT : APP_MEASUREMENT_ERROR_NTC_READ;
  }

  if (s_app_th_meter.latest_ads_debug.ch1_state == APP_TH_METER_ADS_CH_OK)
  {
    out_data->raw_chs1101_code = s_app_th_meter.latest_ads_debug.raw1;
    out_data->valid_flags |= APP_MEASUREMENT_VALID_RAW_CHS1101 | APP_MEASUREMENT_VALID_V_CHS1101;
    (void)algo_humidity_adc_to_rh_diag(out_data->v_chs1101, &rh_adc_diag);
    if (rh_adc_diag.reason == ALGO_SENSOR_DIAG_OK)
    {
      out_data->valid_flags |= APP_MEASUREMENT_VALID_RH_ADC | APP_MEASUREMENT_VALID_HUMIDITY;
      out_data->humidity_rh = out_data->rh_adc;
    }
    else
    {
      out_data->error_flags |= APP_MEASUREMENT_ERROR_CHS_RANGE;
    }
  }
  else
  {
    out_data->error_flags |= (s_app_th_meter.latest_ads_debug.ch1_state == APP_TH_METER_ADS_CH_TIMEOUT) ?
                             APP_MEASUREMENT_ERROR_CHS_TIMEOUT : APP_MEASUREMENT_ERROR_CHS_READ;
  }

  if (isfinite(out_data->rh_pwm))
  {
    out_data->valid_flags |= APP_MEASUREMENT_VALID_RH_PWM;
    if ((drv_humidity_pwm_get_status(&pwm_status) == HAL_OK) && pwm_status.has_valid_sample)
    {
      out_data->pwm_period_ticks = pwm_status.last_period_ticks;
      out_data->pwm_high_ticks = pwm_status.last_high_ticks;
      out_data->valid_flags |= APP_MEASUREMENT_VALID_PWM_TICKS;
    }
  }
  else
  {
    out_data->error_flags |= APP_MEASUREMENT_ERROR_PWM_NO_SAMPLE;
  }

  /* Software validity does not certify sensor calibration or the physical chain. */
  out_data->valid = ((out_data->valid_flags &
                      (APP_MEASUREMENT_VALID_TEMPERATURE | APP_MEASUREMENT_VALID_HUMIDITY)) ==
                     (APP_MEASUREMENT_VALID_TEMPERATURE | APP_MEASUREMENT_VALID_HUMIDITY));
  out_data->sample_count = 1U;
}

static void app_th_meter_format_oled_value(char *buffer, size_t buffer_size,
                                               const char *label, float value,
                                               const char *unit)
{
  int32_t tenths;
  uint32_t magnitude;

  if ((buffer == NULL) || (buffer_size == 0U) || (label == NULL) || (unit == NULL))
  {
    return;
  }
  if (!isfinite(value))
  {
    (void)snprintf(buffer, buffer_size, "%sNA%s", label, unit);
    return;
  }

  tenths = (int32_t)(value * 10.0f + ((value >= 0.0f) ? 0.5f : -0.5f));
  magnitude = (uint32_t)((tenths < 0) ? -tenths : tenths);
  (void)snprintf(buffer, buffer_size, "%s%s%lu.%lu%s", label,
                 (tenths < 0) ? "-" : "", (unsigned long)(magnitude / 10U),
                 (unsigned long)(magnitude % 10U), unit);
}

static void app_th_meter_format_oled_fit(char *buffer, size_t buffer_size,
                                         const app_th_meter_sample_t *sample)
{
  uint16_t profile = app_th_meter_effective_calibration();
  /* A selected profile may be inactive outside its supported domain. SIM and
   * invalid temperature never acquire a calibration claim from the selection. */
  if (s_hybrid_mode.effective_temperature != APP_MEASUREMENT_SOURCE_REAL)
  {
    (void)snprintf(buffer, buffer_size, "CAL:NONE SIM");
  }
  else if ((sample->valid_flags & APP_MEASUREMENT_VALID_TEMPERATURE) == 0U ||
           !isfinite(sample->temperature_c))
  {
    (void)snprintf(buffer, buffer_size, "CAL:%u NA", (unsigned int)profile);
  }
  else if (profile == 0U)
  {
    (void)snprintf(buffer, buffer_size, "CAL:NONE");
  }
  else
  {
    (void)snprintf(buffer, buffer_size, "CAL:%u %s", (unsigned int)profile,
                   sample->calibration_id != 0U ? "PROV" : "BASE");
  }
}

static HAL_StatusTypeDef app_th_meter_show_oled_page(const app_th_meter_sample_t *sample,
                                                     const char *temperature_text,
                                                     uint8_t *temperature_shown)
{
  drv_humidity_pwm_status_t pwm_status = {0};
  const char *ntc_state_short = NULL;
  const char *rh_adc_state_short = NULL;
  const char *pwm_state_short = NULL;
  uint8_t page_index = 0U;
  char lines[4][24] = {{0}};
  char fit_text[24];
  char humidity_text[24];
  HAL_StatusTypeDef status;

  if ((sample == NULL) || (temperature_text == NULL) || (temperature_shown == NULL))
  {
    return HAL_ERROR;
  }
  *temperature_shown = 0U;
  app_th_meter_format_oled_fit(fit_text, sizeof(fit_text), sample);
  app_th_meter_format_oled_value(humidity_text, sizeof(humidity_text), "RH:",
    (sample->valid_flags & APP_MEASUREMENT_VALID_HUMIDITY) != 0U ? sample->humidity_rh : NAN, "%");

  if (sample->source_type == APP_MEASUREMENT_SOURCE_MIXED)
  {
    (void)snprintf(lines[0],sizeof(lines[0]),"MODE T:%s H:%s",
      s_hybrid_mode.effective_temperature==APP_MEASUREMENT_SOURCE_REAL?"R":"S",
      s_hybrid_mode.effective_humidity==APP_MEASUREMENT_SOURCE_REAL?"R":"S");
    (void)snprintf(lines[1], sizeof(lines[1]), "%s", temperature_text);
    (void)snprintf(lines[2], sizeof(lines[2]), "%s", humidity_text);
    (void)snprintf(lines[3], sizeof(lines[3]), "%s", fit_text);
  }
  else if (sample->source_type == APP_MEASUREMENT_SOURCE_SIMULATION)
  {
    (void)snprintf(lines[0], sizeof(lines[0]), "SIMULATION");
    (void)snprintf(lines[1], sizeof(lines[1]), "%s", temperature_text);
    (void)snprintf(lines[2], sizeof(lines[2]), "%s", humidity_text);
    (void)snprintf(lines[3], sizeof(lines[3]), "%s", fit_text);
  }
  else
  {
    (void)drv_humidity_pwm_get_status(&pwm_status);
    ntc_state_short = app_th_meter_ntc_state_short(sample);
    rh_adc_state_short = app_th_meter_rh_adc_state_short(sample);
    pwm_state_short = app_th_meter_pwm_state_short(&pwm_status);
    page_index = (uint8_t)(s_app_th_meter.oled_page_index % APP_TH_METER_OLED_PAGE_COUNT);
    s_app_th_meter.oled_page_index++;
#if (APP_TH_METER_ENABLE_OLED_DIAG_PAGE != 0U)
    if (page_index == 0U)
    {
      return drv_oled_show_hw_check_page(s_app_th_meter.i2c1_device_count,
        s_app_th_meter.i2c2_device_count, s_app_th_meter.oled_ready ? 1U : 0U,
        app_th_meter_ads_is_not_ready() ? 0U : 1U, pwm_status.has_valid_sample ? 1U : 0U);
    }
    if (page_index == 1U)
    {
      return drv_oled_show_voltage_page(sample->v_ntc, sample->v_chs1101,
                                        ntc_state_short, rh_adc_state_short);
    }
#endif
    /* Application owns validity and fit labels; the unchanged driver only
     * transports glyphs. Keep the existing four rows and diagnostic rotation. */
    (void)snprintf(lines[0], sizeof(lines[0]), "CALC %.16s", fit_text);
    (void)snprintf(lines[1], sizeof(lines[1]), "%s", temperature_text);
    (void)snprintf(lines[2], sizeof(lines[2]), "T:%.5s %.10s", ntc_state_short, humidity_text);
    (void)snprintf(lines[3], sizeof(lines[3]), "PWM:%.5s", pwm_state_short);
  }
  status = drv_oled_clear();
  if (status != HAL_OK) { return status; }
  for (uint8_t row = 0U; row < 4U; row++)
  {
    status = drv_oled_show_string(0U, (uint8_t)(row * 2U), lines[row]);
    if (status != HAL_OK) { return status; }
  }
  *temperature_shown = 1U;
  return HAL_OK;
}

static void app_th_meter_publish_sample(const app_th_meter_sample_t *sample)
{
  char buffer[APP_TH_METER_LOG_BUFFER_SIZE] = {0};
  int format_length = 0;

  if (sample == NULL)
  {
    return;
  }

  if (sample->source_type == APP_MEASUREMENT_SOURCE_REAL)
  {
    app_th_meter_log_ads_debug();
    app_th_meter_log_ads_state();
    format_length = app_protocol_format_measure_log(buffer,
                                                    sizeof(buffer),
                                                    sample->v_ntc,
                                                    sample->v_chs1101,
                                                    sample->temperature_c,
                                                    sample->rh_pwm,
                                                    sample->rh_adc);
    app_th_meter_write_protocol_line(format_length, buffer);
  }

  format_length = app_protocol_format_measurement_v3(buffer,sizeof(buffer),sample,
      s_hybrid_mode.effective_temperature,s_hybrid_mode.effective_humidity,s_hybrid_mode.effective_generation);
  app_th_meter_write_protocol_line(format_length, buffer);

  if (sample->source_type == APP_MEASUREMENT_SOURCE_REAL)
  {
    app_th_meter_log_sensor_diag(sample);
  }

  {
    char temperature_text[24];
    uint8_t temperature_shown = 0U;
    float display_temperature = (sample->valid_flags & APP_MEASUREMENT_VALID_TEMPERATURE) != 0U
      ? sample->temperature_c : NAN;
    app_th_meter_format_oled_value(temperature_text, sizeof(temperature_text),
                                    "T:", display_temperature, "C");
    HAL_StatusTypeDef oled_status = app_th_meter_show_oled_page(sample,
      temperature_text, &temperature_shown);
    /* One bounded diagnostic/window pairs the intended one-decimal glyph text
     * and all HAL write results with the same TH3 snapshot. It is not a camera
     * observation of panel pixels. No additional sampling or retry is added. */
    format_length = app_protocol_format_oled_temperature(buffer, sizeof(buffer),
      sample->timestamp_ms, display_temperature, s_hybrid_mode.effective_temperature,
      app_th_meter_effective_calibration(), sample->calibration_id,
      temperature_text, temperature_shown, (uint8_t)oled_status);
    app_th_meter_write_protocol_line(format_length, buffer);
  }
}

static void app_th_meter_collect_sample_group(void)
{
  app_th_meter_sample_t sample;

  /* Both selected sources publish the same snapshot; REAL retains its three
   * hardware acquisition paths, while SIM never invents ADC/PWM samples. */
  if(!s_hybrid_mode.channel_selection_applied) app_measurement_source_read(s_app_th_meter.source,&sample,HAL_GetTick());
  else {
    measurement_data_t real,sim;
    uint32_t now=HAL_GetTick();
    const measurement_data_t *real_input=NULL,*sim_input=NULL;
    if(s_hybrid_mode.effective_temperature==APP_MEASUREMENT_SOURCE_REAL || s_hybrid_mode.effective_humidity==APP_MEASUREMENT_SOURCE_REAL) {
      app_measurement_source_read(&s_app_th_meter_real_source,&real,now); real_input=&real;
    }
    if(s_hybrid_mode.effective_temperature==APP_MEASUREMENT_SOURCE_SIMULATION || s_hybrid_mode.effective_humidity==APP_MEASUREMENT_SOURCE_SIMULATION) {
      app_measurement_source_read(&app_measurement_source_sim,&sim,now); sim_input=&sim;
    }
    app_hybrid_compose_channels(&sample,real_input,sim_input,&s_hybrid_mode,now);
  }
  app_th_meter_sample_accumulator_add(&s_app_th_meter.sample_accumulator, &sample);
}

static void app_th_meter_try_collect_sample_group(void)
{
  uint32_t now = HAL_GetTick();
  uint32_t elapsed_ms = now - s_app_th_meter.last_sample_scheduler_tick;

  if (elapsed_ms == 0U)
  {
    return;
  }

  s_app_th_meter.last_sample_scheduler_tick = now;
  s_app_th_meter.sample_group_budget += elapsed_ms * APP_TH_METER_SAMPLE_GROUPS_PER_SECOND;

  /*
   * 预算达到 1000 表示当前 1 秒基准下“攒够 1 组采样权利”。
   * 这样可以把 `16 groups/s` 解释为“每 1000 ms 累积 16 组预算”，
   * 而不是写死某个固定延时；当主循环存在少量抖动时，也更容易保持整窗组数稳定。
   */
  if (s_app_th_meter.sample_group_budget < APP_TH_METER_SAMPLE_BUDGET_UNIT)
  {
    return;
  }

  s_app_th_meter.sample_group_budget -= APP_TH_METER_SAMPLE_BUDGET_UNIT;
  app_th_meter_collect_sample_group();
}

/* Main-loop-only state: requests and publish run in the same scheduler context.
 * A pending choice takes effect after publishing the current window. */
static uint16_t s_calibration_requested=1U, s_calibration_effective=1U;
bool app_th_meter_request_calibration(uint16_t profile) {
  if(profile>3U) { return false; }
  s_calibration_requested=profile; return true;
}
uint16_t app_th_meter_requested_calibration(void) { return s_calibration_requested; }
uint16_t app_th_meter_effective_calibration(void) { return s_calibration_effective; }

static void app_th_meter_try_publish_sample_window(void)
{
  uint32_t now = HAL_GetTick();
  app_th_meter_sample_t averaged_sample;

  /*
   * 对外仍保持 1 s 发布节奏，所以串口和 OLED 观察上看起来依然是“每秒 1 条”。
   * 但窗口内部已经先收集多组样本，并按字段分别对有效值做平均，从而平滑瞬时波动。
   */
  if ((now - s_app_th_meter.last_sample_publish_tick) < APP_TH_METER_SAMPLE_PUBLISH_PERIOD_MS)
  {
    return;
  }

  s_app_th_meter.last_sample_publish_tick = now;
  app_th_meter_sample_accumulator_finalize(&s_app_th_meter.sample_accumulator, &averaged_sample);
  averaged_sample.timestamp_ms = now;
  averaged_sample.source_type=s_hybrid_mode.effective_temperature==s_hybrid_mode.effective_humidity
    ?s_hybrid_mode.effective_temperature:APP_MEASUREMENT_SOURCE_MIXED;
  /* Workbook inputs are published physical means. Apply once after averaging,
   * only to valid REAL temperature; SIM, raw ADC/voltage and humidity stay intact.
   * One diagnostic per existing window preserves original mean float bits. */
  if (s_hybrid_mode.effective_temperature == APP_MEASUREMENT_SOURCE_REAL &&
      (averaged_sample.valid_flags & APP_MEASUREMENT_VALID_TEMPERATURE) != 0U)
  {
    char correction_log[APP_TH_METER_LOG_BUFFER_SIZE] = {0};
    float raw_mean = averaged_sample.temperature_c;
    averaged_sample.temperature_c = algo_temperature_correct_profile(raw_mean, s_calibration_effective,
                                                                   &averaged_sample.calibration_id);
    int correction_length = app_protocol_format_temperature_evidence(correction_log,
      sizeof(correction_log), raw_mean, averaged_sample.temperature_c, averaged_sample.calibration_id,
      now, s_calibration_effective);
    app_th_meter_write_protocol_line(correction_length, correction_log);
  }
  app_th_meter_publish_sample(&averaged_sample);
  s_calibration_effective=s_calibration_requested;

  /* Apply only after the old window is published. Reset both accumulators at
     this shared boundary, so the next frame cannot contain pre-switch raw. */
  bool had_sim=s_hybrid_mode.effective_temperature==APP_MEASUREMENT_SOURCE_SIMULATION
    ||s_hybrid_mode.effective_humidity==APP_MEASUREMENT_SOURCE_SIMULATION;
  if(app_hybrid_apply_mode(&s_hybrid_mode) && !had_sim
    && (s_hybrid_mode.effective_temperature==APP_MEASUREMENT_SOURCE_SIMULATION || s_hybrid_mode.effective_humidity==APP_MEASUREMENT_SOURCE_SIMULATION))
    app_measurement_source_sim.init(now);

  app_th_meter_sample_accumulator_reset(&s_app_th_meter.sample_accumulator);
  s_app_th_meter.sample_group_budget = 0U;
  s_app_th_meter.last_sample_scheduler_tick = now;
}

bool app_th_meter_select_source(app_measurement_source_type_t source_type)
{
  if (s_app_th_meter.initialized ||
      ((source_type != APP_MEASUREMENT_SOURCE_REAL) &&
       (source_type != APP_MEASUREMENT_SOURCE_SIMULATION)))
  {
    return false;
  }

  s_requested_source_type = source_type;
  return true;
}

const app_hybrid_mode_t *app_th_meter_get_mode(void) { return &s_hybrid_mode; }
bool app_th_meter_request_mode(app_measurement_source_type_t temperature,app_measurement_source_type_t humidity) {
  return app_hybrid_request_mode(&s_hybrid_mode,temperature,humidity,APP_ENABLE_REMOTE_SIMULATION!=0,
    s_requested_source_type==APP_MEASUREMENT_SOURCE_REAL);
}

void app_th_meter_init(void)
{
  uint32_t now = HAL_GetTick();
  uint8_t device_count = 0U;

  s_app_th_meter = (app_th_meter_state_t){0};
  app_hybrid_init_mode(&s_hybrid_mode,s_requested_source_type);
  s_app_th_meter.source = (s_requested_source_type == APP_MEASUREMENT_SOURCE_SIMULATION) ?
                          &app_measurement_source_sim : &s_app_th_meter_real_source;
  if (s_app_th_meter.source->init != NULL)
  {
    s_app_th_meter.source->init(now);
  }
  s_app_th_meter.last_led_tick = now;
  s_app_th_meter.last_key_tick = now;
  s_app_th_meter.last_sample_scheduler_tick = now;
  s_app_th_meter.last_sample_publish_tick = now;
  app_th_meter_sample_accumulator_reset(&s_app_th_meter.sample_accumulator);
  app_th_meter_ads_debug_reset(&s_app_th_meter.latest_ads_debug);
  s_app_th_meter.oled_page_index = 0U;

  (void)bsp_uart_log_init(&huart1);
  app_th_meter_log_boot();
  app_th_meter_log_source();

  if (s_app_th_meter.source->type == APP_MEASUREMENT_SOURCE_REAL)
  {
    /* Only REAL probes and starts sensors; SIM never substitutes for a failure. */
    device_count = bsp_i2c_scan_bus(&hi2c1, "I2C1");
    s_app_th_meter.i2c1_device_count = device_count;
    app_th_meter_log_scan_summary("I2C1", device_count);

    device_count = bsp_i2c_scan_bus(&hi2c2, "I2C2");
    s_app_th_meter.i2c2_device_count = device_count;
    app_th_meter_log_scan_summary("I2C2", device_count);
  }

  s_app_th_meter.oled_ready = (drv_oled_init() == HAL_OK);
  if (s_app_th_meter.oled_ready)
  {
    (void)drv_oled_show_boot_page();
  }

  if (s_app_th_meter.source->type == APP_MEASUREMENT_SOURCE_REAL)
  {
    (void)drv_ads1115_init();
    s_app_th_meter.ads1115_ready = ((s_app_th_meter.i2c2_device_count != 0U) && (drv_ads1115_probe() == HAL_OK));

    s_app_th_meter.humidity_pwm_initialized = (drv_humidity_pwm_init() == HAL_OK);
    if (s_app_th_meter.humidity_pwm_initialized)
    {
      s_app_th_meter.humidity_pwm_started = (drv_humidity_pwm_start() == HAL_OK);
    }
  }

  /* [STATUS] 先回答“模块准备好了没有”，[SAMPLE_CFG] 再回答“软件准备怎么采”。 */
  if (s_app_th_meter.source->type == APP_MEASUREMENT_SOURCE_REAL)
  {
    app_th_meter_log_status_summary();
    app_th_meter_log_sampling_summary();
    app_th_meter_log_ref_expect();
  }

  s_app_th_meter.initialized = true;
}

void app_th_meter_loop(void)
{
  uint32_t now = HAL_GetTick();

  if (!s_app_th_meter.initialized)
  {
    return;
  }

  if ((now - s_app_th_meter.last_led_tick) >= APP_TH_METER_LED_PERIOD_MS)
  {
    s_app_th_meter.last_led_tick = now;
    bsp_led_key_led_toggle();
  }

  if (bsp_led_key_is_pressed() && ((now - s_app_th_meter.last_key_tick) >= APP_TH_METER_KEY_REPORT_PERIOD_MS))
  {
    s_app_th_meter.last_key_tick = now;
    app_th_meter_write_text("KEY PRESSED\r\n");
  }

  app_th_meter_try_collect_sample_group();
  app_th_meter_try_publish_sample_window();
  HAL_Delay(1);
}
