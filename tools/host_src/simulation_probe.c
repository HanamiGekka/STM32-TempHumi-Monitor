/* Host-only probe: exercise the production simulation, protocol, and conversion C files. */
#include <stdint.h>
#include <stdio.h>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

#include "algo_sensor_convert.h"
#include "app_measurement_source.h"
#include "app_measurement_source_sim.h"
#include "app_protocol.h"

#define PROBE_SEED 0x12345678U
#define PROBE_DURATION_MS 120000U
#define PROBE_STEP_MS 100U
#define PROBE_FRAME_INTERVAL_MS 1000U

static void probe_emit_ntc_vector(float expected_temp_c)
{
  algo_ntc_diag_t diagnosis;
  float voltage_v = algo_ntc_expected_voltage_from_temp_c(expected_temp_c);
  float actual_temp_c = algo_ntc_voltage_to_temp_c_diag(voltage_v, &diagnosis);

  (void)printf("ALGO,NTC,%.2f,%.6f,%.6f,%s\n", (double)expected_temp_c,
               (double)voltage_v, (double)actual_temp_c,
               algo_sensor_diag_reason_to_str(diagnosis.reason));
}

static void probe_emit_rh_vector(float expected_rh)
{
  algo_rh_adc_diag_t diagnosis;
  float voltage_v = algo_humidity_expected_voltage_from_rh(expected_rh);
  float actual_rh = algo_humidity_adc_to_rh_diag(voltage_v, &diagnosis);

  (void)printf("ALGO,RH,%.2f,%.6f,%.6f,%s\n", (double)expected_rh,
               (double)voltage_v, (double)actual_rh,
               algo_sensor_diag_reason_to_str(diagnosis.reason));
}

int main(void)
{
  uint32_t timestamp_ms;
  measurement_data_t measurement;
  algo_ntc_diag_t ntc_diagnosis;
  algo_rh_adc_diag_t rh_diagnosis;
  char frame[256];

#ifdef _WIN32
  /* TH3 already ends in CRLF; Windows text stdout would add another CR. */
  if (_setmode(_fileno(stdout), _O_BINARY) < 0)
  {
    return 1;
  }
#endif

  app_measurement_source_sim_reset(0U, PROBE_SEED);
  for (timestamp_ms = 0U; timestamp_ms <= PROBE_DURATION_MS;
       timestamp_ms += PROBE_STEP_MS)
  {
    app_measurement_source_read(&app_measurement_source_sim, &measurement, timestamp_ms);
    (void)printf("TRACE,%lu,%.6f,%.6f,%u,%u,%u\n",
                 (unsigned long)measurement.timestamp_ms,
                 (double)measurement.temperature_c,
                 (double)measurement.humidity_rh,
                 measurement.valid ? 1U : 0U,
                 (unsigned int)measurement.valid_flags,
                 (unsigned int)measurement.error_flags);

    if ((timestamp_ms % PROBE_FRAME_INTERVAL_MS) == 0U)
    {
      if (app_protocol_format_measurement_v3(frame, sizeof(frame), &measurement, APP_MEASUREMENT_SOURCE_SIMULATION, APP_MEASUREMENT_SOURCE_SIMULATION, 0U) < 0)
      {
        (void)fputs("TH3 formatting failed\n", stderr);
        return 1;
      }
      (void)fputs(frame, stdout);
    }
  }

  probe_emit_ntc_vector(25.0f);
  probe_emit_ntc_vector(30.0f);
  probe_emit_rh_vector(50.0f);
  probe_emit_rh_vector(90.0f);

  (void)algo_ntc_voltage_to_temp_c_diag(0.0f, &ntc_diagnosis);
  (void)printf("ALGO,NTC_INVALID,0.000000,%s\n",
               algo_sensor_diag_reason_to_str(ntc_diagnosis.reason));
  (void)algo_humidity_adc_to_rh_diag(2.5f, &rh_diagnosis);
  (void)printf("ALGO,RH_INVALID,2.500000,%s\n",
               algo_sensor_diag_reason_to_str(rh_diagnosis.reason));
  return 0;
}
