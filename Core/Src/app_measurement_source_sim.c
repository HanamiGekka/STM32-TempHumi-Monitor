#include "app_measurement_source_sim.h"

#include <stddef.h>

/* Model parameters are for software-link testing only, not sensor calibration. */
#define APP_SIM_STEP_MS                  100U
#define APP_SIM_MAX_CATCHUP_STEPS        100U
#define APP_SIM_MINUTE_MS                60000ULL
#define APP_SIM_INITIAL_TEMPERATURE_C    29.0f
#define APP_SIM_INITIAL_HUMIDITY_RH      60.0f
#define APP_SIM_BASE_TEMP_MIN_C          25.0f
#define APP_SIM_BASE_TEMP_MAX_C          35.0f
#define APP_SIM_BASE_RH_MIN              40.0f
#define APP_SIM_BASE_RH_MAX              80.0f
#define APP_SIM_TEMP_WALK_STEP_C         0.015f
#define APP_SIM_RH_WALK_STEP             0.08f
#define APP_SIM_TEMP_DISTURBANCE_C       15.0f
#define APP_SIM_RH_EVENT_MIN             20.0f
#define APP_SIM_RH_EVENT_SPAN            70.0f
#define APP_SIM_K_MIN                    0.03f
#define APP_SIM_K_SPAN                   0.05f
#define APP_SIM_SEED_MIX                 0xA5C39E71U

typedef struct
{
  uint32_t lfsr;
  uint32_t last_tick_ms;
  uint64_t elapsed_ms;
  uint64_t next_event_ms;
  float temperature_c;
  float humidity_rh;
  float target_temperature_c;
  float target_humidity_rh;
  bool disturbance_active;
  bool returning_to_baseline;
} app_sim_state_t;

static app_sim_state_t s_app_sim = {0};

/* Right-shift Galois LFSR, polynomial x^32+x^22+x^2+x+1. Zero is excluded. */
static uint32_t app_sim_next_u32(void)
{
  uint32_t lsb = s_app_sim.lfsr & 1U;
  s_app_sim.lfsr = (s_app_sim.lfsr >> 1U) ^ ((lsb != 0U) ? 0x80200003U : 0U);
  return s_app_sim.lfsr;
}

static float app_sim_unit_random(void)
{
  return (float)(app_sim_next_u32() & 0xFFFFU) / 65535.0f;
}

static float app_sim_signed_random(void)
{
  return 2.0f * app_sim_unit_random() - 1.0f;
}

static float app_sim_limit(float value, float lower, float upper)
{
  if (value < lower)
  {
    return lower;
  }
  if (value > upper)
  {
    return upper;
  }
  return value;
}

static uint64_t app_sim_event_offset_ms(void)
{
  /* Exactly one selected second in [10, 50] in each simulation minute. */
  return (uint64_t)(10U + (app_sim_next_u32() % 41U)) * 1000ULL;
}

void app_measurement_source_sim_reset(uint32_t timestamp_ms, uint32_t seed)
{
  s_app_sim = (app_sim_state_t){0};
  s_app_sim.lfsr = (seed != 0U) ? seed : APP_SIM_SEED_MIX;
  s_app_sim.last_tick_ms = timestamp_ms;
  s_app_sim.temperature_c = APP_SIM_INITIAL_TEMPERATURE_C;
  s_app_sim.humidity_rh = APP_SIM_INITIAL_HUMIDITY_RH;
  s_app_sim.target_temperature_c = s_app_sim.temperature_c;
  s_app_sim.target_humidity_rh = s_app_sim.humidity_rh;
  s_app_sim.next_event_ms = app_sim_event_offset_ms();
}

static void app_sim_init(uint32_t timestamp_ms)
{
  app_measurement_source_sim_reset(timestamp_ms, timestamp_ms ^ APP_SIM_SEED_MIX);
}

static void app_sim_advance_step(void)
{
  float temperature_walk_c = APP_SIM_TEMP_WALK_STEP_C * app_sim_signed_random();
  float humidity_walk_rh = APP_SIM_RH_WALK_STEP * app_sim_signed_random();

  s_app_sim.elapsed_ms += APP_SIM_STEP_MS;
  if ((s_app_sim.elapsed_ms % APP_SIM_MINUTE_MS) == 0ULL)
  {
    /* Recover toward a fresh summer indoor baseline without snapping values. */
    s_app_sim.target_temperature_c = APP_SIM_BASE_TEMP_MIN_C +
                                     (APP_SIM_BASE_TEMP_MAX_C - APP_SIM_BASE_TEMP_MIN_C) * app_sim_unit_random();
    s_app_sim.target_humidity_rh = APP_SIM_BASE_RH_MIN +
                                   (APP_SIM_BASE_RH_MAX - APP_SIM_BASE_RH_MIN) * app_sim_unit_random();
    s_app_sim.disturbance_active = false;
    s_app_sim.returning_to_baseline = true;
  }
  if (s_app_sim.elapsed_ms >= s_app_sim.next_event_ms)
  {
    /* Targets may leave the baseline range; the output never jumps to a target. */
    s_app_sim.target_temperature_c = s_app_sim.temperature_c +
                                     APP_SIM_TEMP_DISTURBANCE_C * app_sim_signed_random();
    s_app_sim.target_humidity_rh = APP_SIM_RH_EVENT_MIN +
                                  APP_SIM_RH_EVENT_SPAN * app_sim_unit_random();
    s_app_sim.disturbance_active = true;
    s_app_sim.returning_to_baseline = false;
    s_app_sim.next_event_ms = ((s_app_sim.elapsed_ms / APP_SIM_MINUTE_MS) + 1ULL) *
                              APP_SIM_MINUTE_MS + app_sim_event_offset_ms();
  }

  if (s_app_sim.disturbance_active || s_app_sim.returning_to_baseline)
  {
    /* First-order response per 100 ms: k in [0.03, 0.08], with a small walk. */
    float k_temperature = APP_SIM_K_MIN + APP_SIM_K_SPAN * app_sim_unit_random();
    float k_humidity = APP_SIM_K_MIN + APP_SIM_K_SPAN * app_sim_unit_random();
    s_app_sim.temperature_c += k_temperature * (s_app_sim.target_temperature_c - s_app_sim.temperature_c) +
                               temperature_walk_c;
    s_app_sim.humidity_rh += k_humidity * (s_app_sim.target_humidity_rh - s_app_sim.humidity_rh) +
                             humidity_walk_rh;
  }
  else
  {
    /* Baseline summer indoor random walk; only this ordinary state is bounded. */
    s_app_sim.temperature_c = app_sim_limit(s_app_sim.temperature_c + temperature_walk_c,
                                            APP_SIM_BASE_TEMP_MIN_C, APP_SIM_BASE_TEMP_MAX_C);
    s_app_sim.humidity_rh = app_sim_limit(s_app_sim.humidity_rh + humidity_walk_rh,
                                         APP_SIM_BASE_RH_MIN, APP_SIM_BASE_RH_MAX);
  }
}

static void app_sim_read(measurement_data_t *out_data, uint32_t timestamp_ms)
{
  uint32_t elapsed_ticks_ms;
  uint32_t steps;
  uint32_t step_index;

  if (out_data == NULL)
  {
    return;
  }

  elapsed_ticks_ms = timestamp_ms - s_app_sim.last_tick_ms;
  steps = elapsed_ticks_ms / APP_SIM_STEP_MS;
  if (steps > APP_SIM_MAX_CATCHUP_STEPS)
  {
    /* A stalled caller must not trigger an unbounded loop or an output jump. */
    steps = APP_SIM_MAX_CATCHUP_STEPS;
    s_app_sim.last_tick_ms = timestamp_ms - steps * APP_SIM_STEP_MS;
  }

  for (step_index = 0U; step_index < steps; step_index++)
  {
    app_sim_advance_step();
  }
  s_app_sim.last_tick_ms += steps * APP_SIM_STEP_MS;

  out_data->temperature_c = s_app_sim.temperature_c;
  out_data->humidity_rh = s_app_sim.humidity_rh;
  out_data->valid_flags = APP_MEASUREMENT_VALID_TEMPERATURE | APP_MEASUREMENT_VALID_HUMIDITY;
  out_data->valid = true;
  out_data->sample_count = 1U;
}

const app_measurement_source_t app_measurement_source_sim =
{
  APP_MEASUREMENT_SOURCE_SIMULATION,
  app_sim_init,
  app_sim_read
};
