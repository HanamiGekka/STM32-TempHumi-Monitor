#ifndef APP_HYBRID_SOURCE_H
#define APP_HYBRID_SOURCE_H
#include "app_measurement_source.h"
typedef struct {
  app_measurement_source_type_t requested_temperature,requested_humidity;
  app_measurement_source_type_t effective_temperature,effective_humidity;
  uint32_t requested_generation,effective_generation;
  bool pending,channel_selection_applied;
} app_hybrid_mode_t;
void app_hybrid_init_mode(app_hybrid_mode_t *mode,app_measurement_source_type_t initial);
bool app_hybrid_request_mode(app_hybrid_mode_t *mode,app_measurement_source_type_t temperature,
                             app_measurement_source_type_t humidity,bool sim_available,bool real_available);
bool app_hybrid_apply_mode(app_hybrid_mode_t *mode);
void app_hybrid_compose_channels(measurement_data_t *out,const measurement_data_t *real,
                                  const measurement_data_t *sim,const app_hybrid_mode_t *mode,uint32_t timestamp_ms);
#endif
