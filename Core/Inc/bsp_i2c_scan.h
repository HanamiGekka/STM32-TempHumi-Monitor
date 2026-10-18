#ifndef BSP_I2C_SCAN_H
#define BSP_I2C_SCAN_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

uint8_t bsp_i2c_scan_bus(I2C_HandleTypeDef *hi2c, const char *bus_name);

#ifdef __cplusplus
}
#endif

#endif /* BSP_I2C_SCAN_H */
