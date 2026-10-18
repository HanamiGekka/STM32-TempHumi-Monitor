#ifndef DRV_ADS1115_H
#define DRV_ADS1115_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

/* AIN0/AIN1 已映射到当前温度链与湿度模拟链，便于业务层直接按别名调用。 */
#define DRV_ADS1115_CHANNEL_0  0U
#define DRV_ADS1115_CHANNEL_1  1U
#define DRV_ADS1115_CHANNEL_2  2U
#define DRV_ADS1115_CHANNEL_3  3U

#define DRV_ADS1115_CHANNEL_AIN0        DRV_ADS1115_CHANNEL_0
#define DRV_ADS1115_CHANNEL_AIN1        DRV_ADS1115_CHANNEL_1
#define DRV_ADS1115_CHANNEL_AIN2        DRV_ADS1115_CHANNEL_2
#define DRV_ADS1115_CHANNEL_AIN3        DRV_ADS1115_CHANNEL_3

#define DRV_ADS1115_CHANNEL_V_NTC       DRV_ADS1115_CHANNEL_AIN0
#define DRV_ADS1115_CHANNEL_V_CHS1101   DRV_ADS1115_CHANNEL_AIN1

typedef enum
{
  DRV_ADS1115_DIAG_NONE = 0,
  DRV_ADS1115_DIAG_NOT_INITIALIZED,
  DRV_ADS1115_DIAG_NULL_ARG,
  DRV_ADS1115_DIAG_INVALID_CHANNEL,
  DRV_ADS1115_DIAG_NO_ADS,
  DRV_ADS1115_DIAG_PROBE_FAIL,
  DRV_ADS1115_DIAG_CONFIG_WRITE_FAIL,
  DRV_ADS1115_DIAG_CONFIG_READ_FAIL,
  DRV_ADS1115_DIAG_OS_TIMEOUT,
  DRV_ADS1115_DIAG_CONVERSION_READ_FAIL
} drv_ads1115_diag_reason_t;

HAL_StatusTypeDef drv_ads1115_init(void);
HAL_StatusTypeDef drv_ads1115_probe(void);
/* read_raw 成功返回 HAL_OK；参数、I2C 读写失败返回 HAL_ERROR；OS 位等待超时返回 HAL_TIMEOUT。 */
HAL_StatusTypeDef drv_ads1115_read_raw(uint8_t channel, int16_t *out_raw);
HAL_StatusTypeDef drv_ads1115_get_last_diag(drv_ads1115_diag_reason_t *out_reason);
float drv_ads1115_raw_to_voltage(int16_t raw);

#ifdef __cplusplus
}
#endif

#endif /* DRV_ADS1115_H */
