#ifndef BSP_UART_LOG_H
#define BSP_UART_LOG_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

HAL_StatusTypeDef bsp_uart_log_init(UART_HandleTypeDef *huart);
HAL_StatusTypeDef bsp_uart_log_write(const char *str);

#ifdef __cplusplus
}
#endif

#endif /* BSP_UART_LOG_H */
