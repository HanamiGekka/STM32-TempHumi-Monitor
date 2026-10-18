#ifndef APP_UART_COMMAND_H
#define APP_UART_COMMAND_H
#include "stm32f1xx_hal.h"
void app_uart_command_init(UART_HandleTypeDef *uart);
void app_uart_command_loop(void);
#endif
