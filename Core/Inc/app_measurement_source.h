#ifndef APP_MEASUREMENT_SOURCE_H
#define APP_MEASUREMENT_SOURCE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

typedef enum
{
  APP_MEASUREMENT_SOURCE_REAL = 0,
  APP_MEASUREMENT_SOURCE_SIMULATION = 1,
  APP_MEASUREMENT_SOURCE_MIXED = 2
} app_measurement_source_type_t;

/* Field validity is independent of the overall temperature/humidity result. */
typedef enum
{
  APP_MEASUREMENT_VALID_TEMPERATURE = 1U << 0,
  APP_MEASUREMENT_VALID_HUMIDITY = 1U << 1,
  APP_MEASUREMENT_VALID_V_NTC = 1U << 2,
  APP_MEASUREMENT_VALID_V_CHS1101 = 1U << 3,
  APP_MEASUREMENT_VALID_RH_PWM = 1U << 4,
  APP_MEASUREMENT_VALID_RH_ADC = 1U << 5,
  APP_MEASUREMENT_VALID_RAW_NTC = 1U << 6,
  APP_MEASUREMENT_VALID_RAW_CHS1101 = 1U << 7,
  APP_MEASUREMENT_VALID_PWM_TICKS = 1U << 8
} app_measurement_valid_flag_t;

typedef enum
{
  APP_MEASUREMENT_ERROR_NONE = 0U,
  APP_MEASUREMENT_ERROR_ADS_NOT_READY = 1U << 0,
  APP_MEASUREMENT_ERROR_NTC_READ = 1U << 1,
  APP_MEASUREMENT_ERROR_CHS_READ = 1U << 2,
  APP_MEASUREMENT_ERROR_NTC_RANGE = 1U << 3,
  APP_MEASUREMENT_ERROR_CHS_RANGE = 1U << 4,
  APP_MEASUREMENT_ERROR_PWM_NO_SAMPLE = 1U << 5,
  APP_MEASUREMENT_ERROR_NTC_TIMEOUT = 1U << 6,
  APP_MEASUREMENT_ERROR_CHS_TIMEOUT = 1U << 7,
  APP_MEASUREMENT_ERROR_SOURCE_UNAVAILABLE = 1U << 8
} app_measurement_error_flag_t;

typedef struct
{
  float temperature_c;       /* degrees C; may retain a finite diagnostic outlier */
  float humidity_rh;         /* percent RH; canonical result uses the ADC chain */
  float v_ntc;               /* V; NAN when no physical ADC result exists */
  float v_chs1101;           /* V; NAN when no physical ADC result exists */
  float rh_pwm;              /* percent RH; diagnostic PWM conversion */
  float rh_adc;              /* percent RH; diagnostic ADC conversion */
  int16_t raw_ntc_code;      /* ADS1115 code; inspect valid_flags before use */
  int16_t raw_chs1101_code;  /* ADS1115 code; inspect valid_flags before use */
  uint32_t pwm_period_ticks; /* latest captured period; no producer yet */
  uint32_t pwm_high_ticks;   /* latest captured high interval; no producer yet */
  uint32_t timestamp_ms;     /* HAL tick domain, wraps modulo 2^32 */
  uint16_t valid_flags;
  uint16_t error_flags;
    uint16_t calibration_id;   /* 0 = uncorrected; 1..3 = provisional RUN 3.1 REAL-T profiles,
                             * never a certified accuracy or humidity calibration claim */
  uint16_t sample_count;     /* source reads in this published window */
  bool valid;                /* both canonical temperature and humidity are valid */
  app_measurement_source_type_t source_type;
} measurement_data_t;

typedef struct
{
  app_measurement_source_type_t type;
  void (*init)(uint32_t timestamp_ms);
  void (*read)(measurement_data_t *out_data, uint32_t timestamp_ms);
} app_measurement_source_t;

void app_measurement_source_reset_data(measurement_data_t *out_data,
                                       app_measurement_source_type_t source_type,
                                       uint32_t timestamp_ms);
void app_measurement_source_read(const app_measurement_source_t *source,
                                 measurement_data_t *out_data,
                                 uint32_t timestamp_ms);

#ifdef __cplusplus
}
#endif

#endif /* APP_MEASUREMENT_SOURCE_H */
