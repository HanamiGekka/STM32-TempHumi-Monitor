#ifndef DRV_HUMIDITY_PWM_H
#define DRV_HUMIDITY_PWM_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>

#include "main.h"

typedef struct
{
  /* initialized 与 started 分开记录，便于区分“已建模块”和“已开捕获”。 */
  bool initialized;
  bool started;
  /* has_valid_sample 为 false 时，上层应把 duty 视为“明确无样本”，而不是 0%。 */
  bool has_valid_sample;
  uint32_t last_period_ticks;
  uint32_t last_high_ticks;
  float last_duty;
} drv_humidity_pwm_status_t;

HAL_StatusTypeDef drv_humidity_pwm_init(void);
HAL_StatusTypeDef drv_humidity_pwm_start(void);
/* HAL_OK=有样本，HAL_BUSY=模块未准备好或尚无样本，HAL_ERROR=参数非法。 */
HAL_StatusTypeDef drv_humidity_pwm_get_duty(float *out_duty);
HAL_StatusTypeDef drv_humidity_pwm_get_status(drv_humidity_pwm_status_t *out_status);
HAL_StatusTypeDef drv_humidity_pwm_update_capture_sample(uint32_t period_ticks, uint32_t high_ticks);

#ifdef __cplusplus
}
#endif

#endif /* DRV_HUMIDITY_PWM_H */
