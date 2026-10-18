#ifndef APP_TH_METER_H
#define APP_TH_METER_H

#include <stdbool.h>

#include "app_measurement_source.h"
#include "app_hybrid_source.h"

#ifdef __cplusplus
extern "C" {
#endif

void app_th_meter_init(void);
void app_th_meter_loop(void);
/* Select explicitly before init; REAL remains the power-on default. */
bool app_th_meter_select_source(app_measurement_source_type_t source_type);
const app_hybrid_mode_t *app_th_meter_get_mode(void);
bool app_th_meter_request_mode(app_measurement_source_type_t temperature,app_measurement_source_type_t humidity);
bool app_th_meter_request_calibration(uint16_t profile);
uint16_t app_th_meter_requested_calibration(void);
uint16_t app_th_meter_effective_calibration(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_TH_METER_H */
