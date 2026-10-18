#include "app_hybrid_source.h"
#include <stddef.h>
#include <math.h>
enum {
  APP_HYBRID_T_VALID=APP_MEASUREMENT_VALID_TEMPERATURE|APP_MEASUREMENT_VALID_V_NTC|APP_MEASUREMENT_VALID_RAW_NTC,
  APP_HYBRID_H_VALID=APP_MEASUREMENT_VALID_HUMIDITY|APP_MEASUREMENT_VALID_V_CHS1101|APP_MEASUREMENT_VALID_RH_PWM|APP_MEASUREMENT_VALID_RH_ADC|APP_MEASUREMENT_VALID_RAW_CHS1101|APP_MEASUREMENT_VALID_PWM_TICKS,
  APP_HYBRID_T_ERROR=APP_MEASUREMENT_ERROR_ADS_NOT_READY|APP_MEASUREMENT_ERROR_NTC_READ|APP_MEASUREMENT_ERROR_NTC_RANGE|APP_MEASUREMENT_ERROR_NTC_TIMEOUT|APP_MEASUREMENT_ERROR_SOURCE_UNAVAILABLE,
  APP_HYBRID_H_ERROR=APP_MEASUREMENT_ERROR_ADS_NOT_READY|APP_MEASUREMENT_ERROR_CHS_READ|APP_MEASUREMENT_ERROR_CHS_RANGE|APP_MEASUREMENT_ERROR_PWM_NO_SAMPLE|APP_MEASUREMENT_ERROR_CHS_TIMEOUT|APP_MEASUREMENT_ERROR_SOURCE_UNAVAILABLE
};
void app_hybrid_init_mode(app_hybrid_mode_t *mode,app_measurement_source_type_t initial) {
  *mode=(app_hybrid_mode_t){0};
  mode->requested_temperature=mode->requested_humidity=initial;
  mode->effective_temperature=mode->effective_humidity=initial;
}
bool app_hybrid_request_mode(app_hybrid_mode_t *mode,app_measurement_source_type_t temperature,
                             app_measurement_source_type_t humidity,bool sim_available,bool real_available) {
  if((temperature!=APP_MEASUREMENT_SOURCE_REAL && temperature!=APP_MEASUREMENT_SOURCE_SIMULATION)
     ||(humidity!=APP_MEASUREMENT_SOURCE_REAL && humidity!=APP_MEASUREMENT_SOURCE_SIMULATION)) return false;
  if(!sim_available && (temperature==APP_MEASUREMENT_SOURCE_SIMULATION || humidity==APP_MEASUREMENT_SOURCE_SIMULATION)) return false;
  if(!real_available && (temperature==APP_MEASUREMENT_SOURCE_REAL || humidity==APP_MEASUREMENT_SOURCE_REAL)) return false;
  mode->requested_temperature=temperature; mode->requested_humidity=humidity;
  ++mode->requested_generation; if(mode->requested_generation==0U) ++mode->requested_generation;
  mode->pending=true; return true;
}
bool app_hybrid_apply_mode(app_hybrid_mode_t *mode) {
  if(!mode->pending) return false;
  mode->effective_temperature=mode->requested_temperature; mode->effective_humidity=mode->requested_humidity;
  mode->effective_generation=mode->requested_generation; mode->pending=false; mode->channel_selection_applied=true;
  return true;
}
void app_hybrid_compose_channels(measurement_data_t *out,const measurement_data_t *real,
                                  const measurement_data_t *sim,const app_hybrid_mode_t *mode,uint32_t timestamp_ms) {
  const measurement_data_t *temperature=mode->effective_temperature==APP_MEASUREMENT_SOURCE_REAL?real:sim;
  const measurement_data_t *humidity=mode->effective_humidity==APP_MEASUREMENT_SOURCE_REAL?real:sim;
  app_measurement_source_type_t source=mode->effective_temperature==mode->effective_humidity?mode->effective_temperature:APP_MEASUREMENT_SOURCE_MIXED;
  app_measurement_source_reset_data(out,source,timestamp_ms);
  if(temperature!=NULL) {
    out->temperature_c=temperature->temperature_c;
    out->valid_flags|=temperature->valid_flags&APP_MEASUREMENT_VALID_TEMPERATURE;
    out->error_flags|=temperature->error_flags&APP_HYBRID_T_ERROR;
    if(mode->effective_temperature==APP_MEASUREMENT_SOURCE_REAL) {
      out->v_ntc=temperature->v_ntc; out->raw_ntc_code=temperature->raw_ntc_code;
      out->valid_flags|=temperature->valid_flags&APP_HYBRID_T_VALID;
    }
  } else out->error_flags|=APP_MEASUREMENT_ERROR_SOURCE_UNAVAILABLE;
  if(humidity!=NULL) {
    out->humidity_rh=humidity->humidity_rh;
    out->valid_flags|=humidity->valid_flags&APP_MEASUREMENT_VALID_HUMIDITY;
    out->error_flags|=humidity->error_flags&APP_HYBRID_H_ERROR;
    if(mode->effective_humidity==APP_MEASUREMENT_SOURCE_REAL) {
      out->v_chs1101=humidity->v_chs1101; out->raw_chs1101_code=humidity->raw_chs1101_code;
      out->rh_adc=humidity->rh_adc; out->rh_pwm=humidity->rh_pwm;
      out->pwm_period_ticks=humidity->pwm_period_ticks; out->pwm_high_ticks=humidity->pwm_high_ticks;
      out->valid_flags|=humidity->valid_flags&APP_HYBRID_H_VALID;
    }
  } else out->error_flags|=APP_MEASUREMENT_ERROR_SOURCE_UNAVAILABLE;
  if(!isfinite(out->temperature_c)) out->valid_flags&=(uint16_t)~APP_MEASUREMENT_VALID_TEMPERATURE;
  if(!isfinite(out->humidity_rh)) out->valid_flags&=(uint16_t)~APP_MEASUREMENT_VALID_HUMIDITY;
  out->valid=(out->valid_flags&(APP_MEASUREMENT_VALID_TEMPERATURE|APP_MEASUREMENT_VALID_HUMIDITY))==(APP_MEASUREMENT_VALID_TEMPERATURE|APP_MEASUREMENT_VALID_HUMIDITY);
  out->sample_count=1U;
  if(real!=NULL && source!=APP_MEASUREMENT_SOURCE_SIMULATION) out->calibration_id=real->calibration_id;
}
