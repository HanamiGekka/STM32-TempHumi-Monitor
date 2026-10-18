#include "bsp_uart_log.h"

#include <stddef.h>
#include <string.h>

static UART_HandleTypeDef *s_log_huart = NULL;

HAL_StatusTypeDef bsp_uart_log_init(UART_HandleTypeDef *huart)
{
  if (huart == NULL)
  {
    return HAL_ERROR;
  }

  s_log_huart = huart;
  return HAL_OK;
}

HAL_StatusTypeDef bsp_uart_log_write(const char *str)
{
  uint16_t length = 0U;

  if ((s_log_huart == NULL) || (str == NULL))
  {
    return HAL_ERROR;
  }

  length = (uint16_t)strlen(str);
  return HAL_UART_Transmit(s_log_huart, (uint8_t *)str, length, HAL_MAX_DELAY);
}
