#include "algo_sensor_convert.h"

#include <math.h>

/* RUN 3.1 model_decision.md: ambient Excel rows 2..6 median offset, deg C.
 * Fade only INSIDE the observed DUT domain; never extrapolate to 50 deg C.
 * Upper fade width 3 deg C keeps d(Tcorr)/d(Traw) >= 0.55. No hardware
 * resistance/beta/gain constants are fitted or changed by this correction. */
#define ALGO_TEMP_CORR_OFFSET_DEG_C 0.9f
#define ALGO_TEMP_CORR_LOW_DEG_C 25.2f
#define ALGO_TEMP_CORR_FULL_LOW_DEG_C 25.7f
#define ALGO_TEMP_CORR_FULL_HIGH_DEG_C 32.6f
#define ALGO_TEMP_CORR_HIGH_DEG_C 35.6f

static float algo_temperature_smooth_weight(float fraction)
{
  return fraction * fraction * (3.0f - 2.0f * fraction);
}

float algo_temperature_correct_mean(float physical_mean_deg_c, bool enabled,
                                    uint16_t *correction_id)
{
  return algo_temperature_correct_profile(physical_mean_deg_c, enabled ? 1U : 0U, correction_id);
}

float algo_temperature_correct_profile(float physical_mean_deg_c, uint16_t profile,
                                      uint16_t *correction_id)
{
  float weight = 1.0f;
  float offset_deg_c = ALGO_TEMP_CORR_OFFSET_DEG_C;
  if (correction_id != NULL) { *correction_id = 0U; }
  if (!ALGO_TEMPERATURE_CORRECTION_ENABLED || profile == 0U || profile > 3U || !isfinite(physical_mean_deg_c) ||
      physical_mean_deg_c <= ALGO_TEMP_CORR_LOW_DEG_C ||
      physical_mean_deg_c >= ALGO_TEMP_CORR_HIGH_DEG_C)
  {
    return physical_mean_deg_c;
  }
  if (physical_mean_deg_c < ALGO_TEMP_CORR_FULL_LOW_DEG_C)
  {
    weight = algo_temperature_smooth_weight((physical_mean_deg_c - ALGO_TEMP_CORR_LOW_DEG_C) /
              (ALGO_TEMP_CORR_FULL_LOW_DEG_C - ALGO_TEMP_CORR_LOW_DEG_C));
  }
  else if (physical_mean_deg_c > ALGO_TEMP_CORR_FULL_HIGH_DEG_C)
  {
    weight = algo_temperature_smooth_weight((ALGO_TEMP_CORR_HIGH_DEG_C - physical_mean_deg_c) /
              (ALGO_TEMP_CORR_HIGH_DEG_C - ALGO_TEMP_CORR_FULL_HIGH_DEG_C));
  }
  /* The constrained fits use the same support/envelope as ID1. Coefficients
   * are degrees C at evenly spaced physical-temperature knots; model sweep
   * and method decision record own rows, loss, uncertainty and validation.
   * No conversion constants or input measurements are changed. */
  if (profile >= 2U)
  {
    static const float lad_offsets[] = {0.833333333f, 1.411111111f, 2.292780559f};
    static const float huber_offsets[] = {1.124446414f, 0.582833784f, 1.518275566f,
      1.224151809f, 1.667911569f, 2.000223574f, 3.419523639f};
    const float *offsets = profile == 2U ? lad_offsets : huber_offsets;
    const uint16_t segments = profile == 2U ? 2U : 6U;
    float position = (physical_mean_deg_c - ALGO_TEMP_CORR_LOW_DEG_C) * (float)segments /
      (ALGO_TEMP_CORR_HIGH_DEG_C - ALGO_TEMP_CORR_LOW_DEG_C);
    uint16_t index = (uint16_t)position;
    if (index >= segments) { index = segments - 1U; }
    float fraction = position - (float)index;
    offset_deg_c = offsets[index] + fraction * (offsets[index + 1U] - offsets[index]);
  }
  if (correction_id != NULL) { *correction_id = profile; }
  return physical_mean_deg_c + offset_deg_c * weight;
}

#define ALGO_NTC_R25_OHM             5000.0f
#define ALGO_NTC_BETA_K              3950.0f
#define ALGO_NTC_T25_K               298.15f
#define ALGO_TEMP_DIVIDER_R_OHM      8200.0f
#define ALGO_TEMP_STAGE_GAIN         (1.0f + (10000.0f / 61900.0f))
#define ALGO_TEMP_STAGE_VMAX         (3.3f * ALGO_TEMP_STAGE_GAIN)
#define ALGO_NTC_DENOM_EPS_V         0.001f
#define ALGO_TEMP_DIAG_MIN_C         (-40.0f)
#define ALGO_TEMP_DIAG_MAX_C         125.0f

#define ALGO_RH_DUTY_OFFSET          0.819f
#define ALGO_RH_DUTY_SLOPE           0.001615f

#define ALGO_RH_ADC_OFFSET_V         2.731f
#define ALGO_RH_ADC_SLOPE_V          0.00538f
#define ALGO_ADS_SINGLE_ENDED_MAX_V  4.096f

static float algo_sensor_convert_clamp(float value, float min_value, float max_value)
{
  if (value < min_value)
  {
    return min_value;
  }

  if (value > max_value)
  {
    return max_value;
  }

  return value;
}

static void algo_ntc_diag_reset(float v_adc, algo_ntc_diag_t *diag)
{
  if (diag == NULL)
  {
    return;
  }

  diag->v_adc = v_adc;
  diag->r_ntc = NAN;
  diag->temp_c = NAN;
  diag->reason = ALGO_SENSOR_DIAG_OK;
}

static void algo_rh_adc_diag_reset(float v_adc, algo_rh_adc_diag_t *diag)
{
  if (diag == NULL)
  {
    return;
  }

  diag->v_adc = v_adc;
  diag->rh_raw = NAN;
  diag->rh_clamped = NAN;
  diag->reason = ALGO_SENSOR_DIAG_OK;
}

float algo_ntc_voltage_to_temp_c_diag(float v_adc, algo_ntc_diag_t *diag)
{
  float denominator_v = 0.0f;
  float r_ntc = 0.0f;
  float log_argument = 0.0f;
  float inv_temp_k = 0.0f;
  float temp_c = NAN;

  algo_ntc_diag_reset(v_adc, diag);

  if (!isfinite(v_adc))
  {
    if (diag != NULL)
    {
      diag->reason = ALGO_SENSOR_DIAG_INPUT_NAN;
    }
    return NAN;
  }

  if (v_adc <= 0.0f)
  {
    if (diag != NULL)
    {
      diag->reason = ALGO_SENSOR_DIAG_INPUT_TOO_LOW;
    }
    return NAN;
  }

  if (v_adc >= ALGO_TEMP_STAGE_VMAX)
  {
    if (diag != NULL)
    {
      diag->reason = ALGO_SENSOR_DIAG_INPUT_TOO_HIGH;
    }
    return NAN;
  }

  denominator_v = ALGO_TEMP_STAGE_VMAX - v_adc;
  if (denominator_v <= ALGO_NTC_DENOM_EPS_V)
  {
    if (diag != NULL)
    {
      diag->reason = ALGO_SENSOR_DIAG_DENOM_TOO_SMALL;
    }
    return NAN;
  }

  r_ntc = (ALGO_TEMP_DIVIDER_R_OHM * v_adc) / denominator_v;
  if ((!isfinite(r_ntc)) || (r_ntc <= 0.0f))
  {
    if (diag != NULL)
    {
      diag->r_ntc = r_ntc;
      diag->reason = ALGO_SENSOR_DIAG_RESISTANCE_INVALID;
    }
    return NAN;
  }

  if (diag != NULL)
  {
    diag->r_ntc = r_ntc;
  }

  log_argument = r_ntc / ALGO_NTC_R25_OHM;
  if ((!isfinite(log_argument)) || (log_argument <= 0.0f))
  {
    if (diag != NULL)
    {
      diag->reason = ALGO_SENSOR_DIAG_LOG_INVALID;
    }
    return NAN;
  }

  inv_temp_k = (1.0f / ALGO_NTC_T25_K) + ((1.0f / ALGO_NTC_BETA_K) * logf(log_argument));
  if ((!isfinite(inv_temp_k)) || (inv_temp_k <= 0.0f))
  {
    if (diag != NULL)
    {
      diag->reason = ALGO_SENSOR_DIAG_LOG_INVALID;
    }
    return NAN;
  }

  temp_c = (1.0f / inv_temp_k) - 273.15f;
  if (diag != NULL)
  {
    diag->temp_c = temp_c;
    if ((!isfinite(temp_c)) || (temp_c < ALGO_TEMP_DIAG_MIN_C) || (temp_c > ALGO_TEMP_DIAG_MAX_C))
    {
      diag->reason = ALGO_SENSOR_DIAG_OUTPUT_OUT_OF_RANGE;
    }
  }

  return temp_c;
}

float algo_ntc_voltage_to_temp_c(float v_adc)
{
  return algo_ntc_voltage_to_temp_c_diag(v_adc, NULL);
}

float algo_humidity_duty_to_rh(float duty)
{
  float rh = 0.0f;

  if ((!isfinite(duty)) || (duty < 0.0f) || (duty > 1.0f))
  {
    return NAN;
  }

  rh = (duty - ALGO_RH_DUTY_OFFSET) / ALGO_RH_DUTY_SLOPE;
  return algo_sensor_convert_clamp(rh, 0.0f, 100.0f);
}

float algo_humidity_adc_to_rh_diag(float v_adc, algo_rh_adc_diag_t *diag)
{
  float rh = 0.0f;

  algo_rh_adc_diag_reset(v_adc, diag);

  if (!isfinite(v_adc))
  {
    if (diag != NULL)
    {
      diag->reason = ALGO_SENSOR_DIAG_INPUT_NAN;
    }
    return NAN;
  }

  if (v_adc < 0.0f)
  {
    if (diag != NULL)
    {
      diag->reason = ALGO_SENSOR_DIAG_INPUT_TOO_LOW;
    }
    return NAN;
  }

  if (v_adc > ALGO_ADS_SINGLE_ENDED_MAX_V)
  {
    if (diag != NULL)
    {
      diag->reason = ALGO_SENSOR_DIAG_INPUT_TOO_HIGH;
    }
    return NAN;
  }

  rh = (v_adc - ALGO_RH_ADC_OFFSET_V) / ALGO_RH_ADC_SLOPE_V;
  if (diag != NULL)
  {
    diag->rh_raw = rh;
  }

  if (rh < 0.0f)
  {
    if (diag != NULL)
    {
      diag->rh_clamped = 0.0f;
      diag->reason = ALGO_SENSOR_DIAG_CLAMPED_LOW;
    }
    return 0.0f;
  }

  if (rh > 100.0f)
  {
    if (diag != NULL)
    {
      diag->rh_clamped = 100.0f;
      diag->reason = ALGO_SENSOR_DIAG_CLAMPED_HIGH;
    }
    return 100.0f;
  }

  if (diag != NULL)
  {
    diag->rh_clamped = rh;
  }

  return rh;
}

float algo_humidity_adc_to_rh(float v_adc)
{
  return algo_humidity_adc_to_rh_diag(v_adc, NULL);
}

const char *algo_sensor_diag_reason_to_str(algo_sensor_diag_reason_t reason)
{
  switch (reason)
  {
    case ALGO_SENSOR_DIAG_OK:
      return "ok";

    case ALGO_SENSOR_DIAG_INPUT_NAN:
      return "input_nan";

    case ALGO_SENSOR_DIAG_INPUT_TOO_LOW:
      return "input_too_low";

    case ALGO_SENSOR_DIAG_INPUT_TOO_HIGH:
      return "input_too_high";

    case ALGO_SENSOR_DIAG_DENOM_TOO_SMALL:
      return "denom_too_small";

    case ALGO_SENSOR_DIAG_RESISTANCE_INVALID:
      return "resistance_invalid";

    case ALGO_SENSOR_DIAG_LOG_INVALID:
      return "log_invalid";

    case ALGO_SENSOR_DIAG_OUTPUT_OUT_OF_RANGE:
      return "output_out_of_range";

    case ALGO_SENSOR_DIAG_CLAMPED_LOW:
      return "below_baseline";

    case ALGO_SENSOR_DIAG_CLAMPED_HIGH:
      return "clamped_high";

    default:
      return "unknown";
  }
}

float algo_ntc_expected_voltage_from_temp_c(float temp_c)
{
  float temp_k = temp_c + 273.15f;
  float r_ntc = 0.0f;

  if ((!isfinite(temp_c)) || (temp_k <= 0.0f))
  {
    return NAN;
  }

  r_ntc = ALGO_NTC_R25_OHM * expf(ALGO_NTC_BETA_K * ((1.0f / temp_k) - (1.0f / ALGO_NTC_T25_K)));
  if ((!isfinite(r_ntc)) || (r_ntc <= 0.0f))
  {
    return NAN;
  }

  return (ALGO_TEMP_STAGE_VMAX * r_ntc) / (ALGO_TEMP_DIVIDER_R_OHM + r_ntc);
}

float algo_humidity_expected_voltage_from_rh(float rh)
{
  if (!isfinite(rh))
  {
    return NAN;
  }

  return ALGO_RH_ADC_OFFSET_V + (ALGO_RH_ADC_SLOPE_V * rh);
}
