#include "app_measurement_source.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

void app_measurement_source_reset_data(measurement_data_t *out_data,
                                       app_measurement_source_type_t source_type,
                                       uint32_t timestamp_ms)
{
  if (out_data == NULL)
  {
    return;
  }

  (void)memset(out_data, 0, sizeof(*out_data));
  out_data->temperature_c = NAN;
  out_data->humidity_rh = NAN;
  out_data->v_ntc = NAN;
  out_data->v_chs1101 = NAN;
  out_data->rh_pwm = NAN;
  out_data->rh_adc = NAN;
  out_data->timestamp_ms = timestamp_ms;
  out_data->source_type = source_type;
}

void app_measurement_source_read(const app_measurement_source_t *source,
                                 measurement_data_t *out_data,
                                 uint32_t timestamp_ms)
{
  if (out_data == NULL)
  {
    return;
  }

  if ((source == NULL) || (source->read == NULL))
  {
    app_measurement_source_reset_data(out_data, APP_MEASUREMENT_SOURCE_REAL, timestamp_ms);
    out_data->error_flags = APP_MEASUREMENT_ERROR_ADS_NOT_READY;
    return;
  }

  app_measurement_source_reset_data(out_data, source->type, timestamp_ms);
  source->read(out_data, timestamp_ms);
}
