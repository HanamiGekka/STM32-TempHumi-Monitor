#include "bsp_i2c_scan.h"

#include <stddef.h>

#include "bsp_uart_log.h"

#define BSP_I2C_SCAN_ADDR_FIRST  0x08U
#define BSP_I2C_SCAN_ADDR_LAST   0x77U
#define BSP_I2C_SCAN_TRIALS      2U
#define BSP_I2C_SCAN_TIMEOUT_MS  10U
#define BSP_I2C_SCAN_LOG_PREFIX  "[I2C_SCAN] "

static uint32_t bsp_i2c_scan_append_char(char *buffer, uint32_t index, uint32_t size, char ch)
{
  if ((buffer == NULL) || (size == 0U))
  {
    return index;
  }

  if ((index + 1U) < size)
  {
    buffer[index] = ch;
    index++;
    buffer[index] = '\0';
  }

  return index;
}

static uint32_t bsp_i2c_scan_append_str(char *buffer, uint32_t index, uint32_t size, const char *str)
{
  if (str == NULL)
  {
    return index;
  }

  while (*str != '\0')
  {
    index = bsp_i2c_scan_append_char(buffer, index, size, *str);
    str++;
  }

  return index;
}

static uint32_t bsp_i2c_scan_append_hex8(char *buffer, uint32_t index, uint32_t size, uint8_t value)
{
  static const char hex_table[] = "0123456789ABCDEF";

  index = bsp_i2c_scan_append_char(buffer, index, size, '0');
  index = bsp_i2c_scan_append_char(buffer, index, size, 'x');
  index = bsp_i2c_scan_append_char(buffer, index, size, hex_table[(value >> 4) & 0x0FU]);
  index = bsp_i2c_scan_append_char(buffer, index, size, hex_table[value & 0x0FU]);

  return index;
}

static uint32_t bsp_i2c_scan_append_u8(char *buffer, uint32_t index, uint32_t size, uint8_t value)
{
  char digits[3];
  uint32_t digit_count = 0U;

  do
  {
    digits[digit_count] = (char)('0' + (value % 10U));
    value /= 10U;
    digit_count++;
  } while ((value > 0U) && (digit_count < sizeof(digits)));

  while (digit_count > 0U)
  {
    digit_count--;
    index = bsp_i2c_scan_append_char(buffer, index, size, digits[digit_count]);
  }

  return index;
}

static void bsp_i2c_scan_log_start(const char *bus_name)
{
  char log_buffer[48] = {0};
  uint32_t index = 0U;

  index = bsp_i2c_scan_append_str(log_buffer, index, sizeof(log_buffer), BSP_I2C_SCAN_LOG_PREFIX);
  index = bsp_i2c_scan_append_str(log_buffer, index, sizeof(log_buffer), bus_name);
  index = bsp_i2c_scan_append_str(log_buffer, index, sizeof(log_buffer), " scan start\r\n");

  (void)bsp_uart_log_write(log_buffer);
}

static void bsp_i2c_scan_log_found(const char *bus_name, uint8_t address)
{
  char log_buffer[56] = {0};
  uint32_t index = 0U;

  index = bsp_i2c_scan_append_str(log_buffer, index, sizeof(log_buffer), BSP_I2C_SCAN_LOG_PREFIX);
  index = bsp_i2c_scan_append_str(log_buffer, index, sizeof(log_buffer), bus_name);
  index = bsp_i2c_scan_append_str(log_buffer, index, sizeof(log_buffer), " found device at ");
  index = bsp_i2c_scan_append_hex8(log_buffer, index, sizeof(log_buffer), address);
  index = bsp_i2c_scan_append_str(log_buffer, index, sizeof(log_buffer), "\r\n");

  (void)bsp_uart_log_write(log_buffer);
}

static void bsp_i2c_scan_log_done(const char *bus_name, uint8_t device_count)
{
  char log_buffer[56] = {0};
  uint32_t index = 0U;

  index = bsp_i2c_scan_append_str(log_buffer, index, sizeof(log_buffer), BSP_I2C_SCAN_LOG_PREFIX);
  index = bsp_i2c_scan_append_str(log_buffer, index, sizeof(log_buffer), bus_name);
  index = bsp_i2c_scan_append_str(log_buffer, index, sizeof(log_buffer), " scan done, devices=");
  index = bsp_i2c_scan_append_u8(log_buffer, index, sizeof(log_buffer), device_count);
  index = bsp_i2c_scan_append_str(log_buffer, index, sizeof(log_buffer), "\r\n");

  (void)bsp_uart_log_write(log_buffer);
}

uint8_t bsp_i2c_scan_bus(I2C_HandleTypeDef *hi2c, const char *bus_name)
{
  const char *resolved_bus_name = (bus_name != NULL) ? bus_name : "I2C?";
  uint8_t address = 0U;
  uint8_t device_count = 0U;

  if (hi2c == NULL)
  {
    bsp_i2c_scan_log_done(resolved_bus_name, 0U);
    return 0U;
  }

  bsp_i2c_scan_log_start(resolved_bus_name);

  for (address = BSP_I2C_SCAN_ADDR_FIRST; address <= BSP_I2C_SCAN_ADDR_LAST; address++)
  {
    if (HAL_I2C_IsDeviceReady(hi2c,
                              ((uint16_t)address << 1),
                              BSP_I2C_SCAN_TRIALS,
                              BSP_I2C_SCAN_TIMEOUT_MS) == HAL_OK)
    {
      device_count++;
      bsp_i2c_scan_log_found(resolved_bus_name, address);
    }
  }

  bsp_i2c_scan_log_done(resolved_bus_name, device_count);

  return device_count;
}
