#ifndef BSP_UART_COMMAND_H
#define BSP_UART_COMMAND_H
#include "stm32f1xx_hal.h"
#include <stdbool.h>
HAL_StatusTypeDef bsp_uart_command_init(UART_HandleTypeDef *uart);
bool bsp_uart_command_read_byte(uint8_t *byte);
uint32_t bsp_uart_command_take_fault(void);
/* RM0008 Rev21 section30.2:96bit UID at0x1FFFF7E8; memory byte order.
   Read-only operational identity, not proof against counterfeit/duplicate silicon. */
void bsp_uart_command_read_identity(uint8_t uid[12], uint16_t *flash_kib, uint16_t *device_id);
#endif
