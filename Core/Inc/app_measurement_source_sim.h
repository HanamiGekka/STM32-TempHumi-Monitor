#ifndef APP_MEASUREMENT_SOURCE_SIM_H
#define APP_MEASUREMENT_SOURCE_SIM_H

#ifdef __cplusplus
extern "C" {
#endif

#include "app_measurement_source.h"

extern const app_measurement_source_t app_measurement_source_sim;

/* Explicit reset seed also makes simulation traces reproducible in host tests. */
void app_measurement_source_sim_reset(uint32_t timestamp_ms, uint32_t seed);

#ifdef __cplusplus
}
#endif

#endif /* APP_MEASUREMENT_SOURCE_SIM_H */
