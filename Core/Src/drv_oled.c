#include "drv_oled.h"

#include "app_version.h"

#include <stdbool.h>
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

extern I2C_HandleTypeDef hi2c1;

#define DRV_OLED_ADDR_PRIMARY       0x3CU
#define DRV_OLED_ADDR_SECONDARY     0x3DU
#define DRV_OLED_READY_TRIALS       2U
#define DRV_OLED_READY_TIMEOUT_MS   10U
#define DRV_OLED_TX_TIMEOUT_MS      50U
#define DRV_OLED_TEXT_BUFFER_SIZE   24U
#define DRV_OLED_WIDTH              128U
#define DRV_OLED_PAGE_COUNT         8U
#define DRV_OLED_CHAR_WIDTH         6U
#define DRV_OLED_GLYPH_WIDTH        5U
#define DRV_OLED_DATA_CHUNK_SIZE    16U

#define DRV_OLED_CONTROL_COMMAND    0x00U
#define DRV_OLED_CONTROL_DATA       0x40U

static bool s_oled_ready = false;
static uint8_t s_oled_addr = 0U;

static const uint8_t s_oled_zero_chunk[DRV_OLED_DATA_CHUNK_SIZE] = {0};

static const uint8_t s_oled_font_space[DRV_OLED_GLYPH_WIDTH]   = {0x00, 0x00, 0x00, 0x00, 0x00};
static const uint8_t s_oled_font_percent[DRV_OLED_GLYPH_WIDTH] = {0x62, 0x64, 0x08, 0x13, 0x23};
static const uint8_t s_oled_font_minus[DRV_OLED_GLYPH_WIDTH]   = {0x08, 0x08, 0x08, 0x08, 0x08};
static const uint8_t s_oled_font_dot[DRV_OLED_GLYPH_WIDTH]     = {0x00, 0x60, 0x60, 0x00, 0x00};
static const uint8_t s_oled_font_slash[DRV_OLED_GLYPH_WIDTH]   = {0x20, 0x10, 0x08, 0x04, 0x02};
static const uint8_t s_oled_font_colon[DRV_OLED_GLYPH_WIDTH]   = {0x00, 0x36, 0x36, 0x00, 0x00};
static const uint8_t s_oled_font_underscore[DRV_OLED_GLYPH_WIDTH] = {0x40, 0x40, 0x40, 0x40, 0x40};

static const uint8_t s_oled_font_0[DRV_OLED_GLYPH_WIDTH] = {0x3E, 0x51, 0x49, 0x45, 0x3E};
static const uint8_t s_oled_font_1[DRV_OLED_GLYPH_WIDTH] = {0x00, 0x42, 0x7F, 0x40, 0x00};
static const uint8_t s_oled_font_2[DRV_OLED_GLYPH_WIDTH] = {0x42, 0x61, 0x51, 0x49, 0x46};
static const uint8_t s_oled_font_3[DRV_OLED_GLYPH_WIDTH] = {0x21, 0x41, 0x45, 0x4B, 0x31};
static const uint8_t s_oled_font_4[DRV_OLED_GLYPH_WIDTH] = {0x18, 0x14, 0x12, 0x7F, 0x10};
static const uint8_t s_oled_font_5[DRV_OLED_GLYPH_WIDTH] = {0x27, 0x45, 0x45, 0x45, 0x39};
static const uint8_t s_oled_font_6[DRV_OLED_GLYPH_WIDTH] = {0x3C, 0x4A, 0x49, 0x49, 0x30};
static const uint8_t s_oled_font_7[DRV_OLED_GLYPH_WIDTH] = {0x01, 0x71, 0x09, 0x05, 0x03};
static const uint8_t s_oled_font_8[DRV_OLED_GLYPH_WIDTH] = {0x36, 0x49, 0x49, 0x49, 0x36};
static const uint8_t s_oled_font_9[DRV_OLED_GLYPH_WIDTH] = {0x06, 0x49, 0x49, 0x29, 0x1E};

static const uint8_t s_oled_font_A[DRV_OLED_GLYPH_WIDTH] = {0x7E, 0x11, 0x11, 0x11, 0x7E};
static const uint8_t s_oled_font_B[DRV_OLED_GLYPH_WIDTH] = {0x7F, 0x49, 0x49, 0x49, 0x36};
static const uint8_t s_oled_font_C[DRV_OLED_GLYPH_WIDTH] = {0x3E, 0x41, 0x41, 0x41, 0x22};
static const uint8_t s_oled_font_D[DRV_OLED_GLYPH_WIDTH] = {0x7F, 0x41, 0x41, 0x22, 0x1C};
static const uint8_t s_oled_font_E[DRV_OLED_GLYPH_WIDTH] = {0x7F, 0x49, 0x49, 0x49, 0x41};
static const uint8_t s_oled_font_H[DRV_OLED_GLYPH_WIDTH] = {0x7F, 0x08, 0x08, 0x08, 0x7F};
static const uint8_t s_oled_font_I[DRV_OLED_GLYPH_WIDTH] = {0x00, 0x41, 0x7F, 0x41, 0x00};
static const uint8_t s_oled_font_K[DRV_OLED_GLYPH_WIDTH] = {0x7F, 0x08, 0x14, 0x22, 0x41};
static const uint8_t s_oled_font_L[DRV_OLED_GLYPH_WIDTH] = {0x7F, 0x40, 0x40, 0x40, 0x40};
static const uint8_t s_oled_font_M[DRV_OLED_GLYPH_WIDTH] = {0x7F, 0x02, 0x04, 0x02, 0x7F};
static const uint8_t s_oled_font_N[DRV_OLED_GLYPH_WIDTH] = {0x7F, 0x04, 0x08, 0x10, 0x7F};
static const uint8_t s_oled_font_O[DRV_OLED_GLYPH_WIDTH] = {0x3E, 0x41, 0x41, 0x41, 0x3E};
static const uint8_t s_oled_font_P[DRV_OLED_GLYPH_WIDTH] = {0x7F, 0x09, 0x09, 0x09, 0x06};
static const uint8_t s_oled_font_R[DRV_OLED_GLYPH_WIDTH] = {0x7F, 0x09, 0x19, 0x29, 0x46};
static const uint8_t s_oled_font_S[DRV_OLED_GLYPH_WIDTH] = {0x46, 0x49, 0x49, 0x49, 0x31};
static const uint8_t s_oled_font_T[DRV_OLED_GLYPH_WIDTH] = {0x01, 0x01, 0x7F, 0x01, 0x01};
static const uint8_t s_oled_font_U[DRV_OLED_GLYPH_WIDTH] = {0x3F, 0x40, 0x40, 0x40, 0x3F};
static const uint8_t s_oled_font_V[DRV_OLED_GLYPH_WIDTH] = {0x1F, 0x20, 0x40, 0x20, 0x1F};
static const uint8_t s_oled_font_W[DRV_OLED_GLYPH_WIDTH] = {0x7F, 0x20, 0x18, 0x20, 0x7F};
static const uint8_t s_oled_font_v[DRV_OLED_GLYPH_WIDTH] = {0x1C, 0x20, 0x40, 0x20, 0x1C};

static HAL_StatusTypeDef drv_oled_probe_address(uint8_t address)
{
  return HAL_I2C_IsDeviceReady(&hi2c1,
                               ((uint16_t)address << 1),
                               DRV_OLED_READY_TRIALS,
                               DRV_OLED_READY_TIMEOUT_MS);
}

static HAL_StatusTypeDef drv_oled_write_command(uint8_t command)
{
  uint8_t tx_buffer[2] = {DRV_OLED_CONTROL_COMMAND, command};

  if (s_oled_addr == 0U)
  {
    return HAL_ERROR;
  }

  return HAL_I2C_Master_Transmit(&hi2c1,
                                 ((uint16_t)s_oled_addr << 1),
                                 tx_buffer,
                                 sizeof(tx_buffer),
                                 DRV_OLED_TX_TIMEOUT_MS);
}

static HAL_StatusTypeDef drv_oled_write_data(const uint8_t *data, uint16_t length)
{
  uint8_t tx_buffer[DRV_OLED_DATA_CHUNK_SIZE + 1U] = {0};
  uint16_t offset = 0U;
  uint16_t chunk_length = 0U;

  if ((s_oled_addr == 0U) || (data == NULL) || (length == 0U))
  {
    return HAL_ERROR;
  }

  tx_buffer[0] = DRV_OLED_CONTROL_DATA;

  while (offset < length)
  {
    chunk_length = (uint16_t)(length - offset);
    if (chunk_length > DRV_OLED_DATA_CHUNK_SIZE)
    {
      chunk_length = DRV_OLED_DATA_CHUNK_SIZE;
    }

    (void)memcpy(&tx_buffer[1], &data[offset], chunk_length);
    if (HAL_I2C_Master_Transmit(&hi2c1,
                                ((uint16_t)s_oled_addr << 1),
                                tx_buffer,
                                (uint16_t)(chunk_length + 1U),
                                DRV_OLED_TX_TIMEOUT_MS) != HAL_OK)
    {
      return HAL_ERROR;
    }

    offset = (uint16_t)(offset + chunk_length);
  }

  return HAL_OK;
}

static HAL_StatusTypeDef drv_oled_set_cursor(uint8_t x, uint8_t y)
{
  if ((x >= DRV_OLED_WIDTH) || (y >= DRV_OLED_PAGE_COUNT))
  {
    return HAL_ERROR;
  }

  if (drv_oled_write_command((uint8_t)(0xB0U + y)) != HAL_OK)
  {
    return HAL_ERROR;
  }

  if (drv_oled_write_command((uint8_t)(0x00U + (x & 0x0FU))) != HAL_OK)
  {
    return HAL_ERROR;
  }

  return drv_oled_write_command((uint8_t)(0x10U + ((x >> 4) & 0x0FU)));
}

static const uint8_t *drv_oled_get_glyph(char ch)
{
  switch (ch)
  {
    case ' ':
      return s_oled_font_space;

    case '%':
      return s_oled_font_percent;

    case '-':
      return s_oled_font_minus;

    case '.':
      return s_oled_font_dot;

    case '/':
      return s_oled_font_slash;

    case ':':
      return s_oled_font_colon;

    case '_':
      return s_oled_font_underscore;

    case '0':
      return s_oled_font_0;

    case '1':
      return s_oled_font_1;

    case '2':
      return s_oled_font_2;

    case '3':
      return s_oled_font_3;

    case '4':
      return s_oled_font_4;

    case '5':
      return s_oled_font_5;

    case '6':
      return s_oled_font_6;

    case '7':
      return s_oled_font_7;

    case '8':
      return s_oled_font_8;

    case '9':
      return s_oled_font_9;

    case 'A':
      return s_oled_font_A;

    case 'B':
      return s_oled_font_B;

    case 'C':
      return s_oled_font_C;

    case 'D':
      return s_oled_font_D;

    case 'E':
      return s_oled_font_E;

    case 'H':
      return s_oled_font_H;

    case 'I':
      return s_oled_font_I;

    case 'K':
      return s_oled_font_K;

    case 'L':
      return s_oled_font_L;

    case 'M':
      return s_oled_font_M;

    case 'N':
      return s_oled_font_N;

    case 'O':
      return s_oled_font_O;

    case 'P':
      return s_oled_font_P;

    case 'R':
      return s_oled_font_R;

    case 'S':
      return s_oled_font_S;

    case 'T':
      return s_oled_font_T;

    case 'U':
      return s_oled_font_U;

    case 'V':
      return s_oled_font_V;

    case 'W':
      return s_oled_font_W;

    case 'v':
      return s_oled_font_v;

    default:
      return s_oled_font_space;
  }
}

static const char *drv_oled_resolve_diag_channel_state(const char *state)
{
  if (state == NULL)
  {
    return "ERROR";
  }

  if ((strcmp(state, "SKIP") == 0) ||
      (strcmp(state, "OK") == 0) ||
      (strcmp(state, "ERROR") == 0) ||
      (strcmp(state, "TIMEOUT") == 0))
  {
    return state;
  }

  return "ERROR";
}

static void drv_oled_format_diag_channel(char *buffer,
                                         uint32_t buffer_size,
                                         uint8_t channel,
                                         const char *state,
                                         uint16_t error_count)
{
  const char *resolved_state = drv_oled_resolve_diag_channel_state(state);

  if ((buffer == NULL) || (buffer_size == 0U))
  {
    return;
  }

  if (strcmp(resolved_state, "SKIP") == 0)
  {
    (void)snprintf(buffer, buffer_size, "A%u:SKIP", (unsigned int)channel);
    return;
  }

  (void)snprintf(buffer,
                 buffer_size,
                 "A%u:%s E:%u",
                 (unsigned int)channel,
                 resolved_state,
                 (unsigned int)error_count);
}

static void drv_oled_format_value(char *buffer,
                                  uint32_t buffer_size,
                                  const char *prefix,
                                  float value,
                                  uint32_t decimals,
                                  const char *suffix)
{
  int32_t scaled_value = 0;
  uint32_t absolute_value = 0U;
  uint32_t scale = 1U;
  uint32_t integer_part = 0U;
  uint32_t fraction_part = 0U;
  uint32_t index = 0U;
  const char *resolved_prefix = (prefix != NULL) ? prefix : "";
  const char *resolved_suffix = (suffix != NULL) ? suffix : "";
  const char *sign = "";

  if ((buffer == NULL) || (buffer_size == 0U))
  {
    return;
  }

  if (decimals > 3U)
  {
    decimals = 3U;
  }

  for (index = 0U; index < decimals; index++)
  {
    scale *= 10U;
  }

  if (!isfinite(value))
  {
    value = -1.0f;
  }

  scaled_value = (value >= 0.0f) ? (int32_t)(value * (float)scale + 0.5f) :
                                   (int32_t)(value * (float)scale - 0.5f);
  if (scaled_value < 0)
  {
    sign = "-";
    absolute_value = (uint32_t)(-scaled_value);
  }
  else
  {
    absolute_value = (uint32_t)scaled_value;
  }

  integer_part = absolute_value / scale;
  fraction_part = absolute_value % scale;

  if (decimals == 0U)
  {
    (void)snprintf(buffer,
                   buffer_size,
                   "%s%s%lu%s",
                   resolved_prefix,
                   sign,
                   (unsigned long)integer_part,
                   resolved_suffix);
  }
  else if (decimals == 1U)
  {
    (void)snprintf(buffer,
                   buffer_size,
                   "%s%s%lu.%01lu%s",
                   resolved_prefix,
                   sign,
                   (unsigned long)integer_part,
                   (unsigned long)fraction_part,
                   resolved_suffix);
  }
  else if (decimals == 2U)
  {
    (void)snprintf(buffer,
                   buffer_size,
                   "%s%s%lu.%02lu%s",
                   resolved_prefix,
                   sign,
                   (unsigned long)integer_part,
                   (unsigned long)fraction_part,
                   resolved_suffix);
  }
  else
  {
    (void)snprintf(buffer,
                   buffer_size,
                   "%s%s%lu.%03lu%s",
                   resolved_prefix,
                   sign,
                   (unsigned long)integer_part,
                   (unsigned long)fraction_part,
                   resolved_suffix);
  }
}

static const char *drv_oled_resolve_short_state(const char *state, const char *fallback)
{
  if ((state != NULL) && (state[0] != '\0'))
  {
    return state;
  }

  return (fallback != NULL) ? fallback : "SKIP";
}

HAL_StatusTypeDef drv_oled_init(void)
{
  /* 先按常见 SSD1306/兼容控制器做保守初始化，后续再按实机细调。 */
  uint8_t init_commands[] = {
    0xAE, 0xD5, 0x80, 0xA8, 0x3F, 0xD3, 0x00, 0x40,
    0x8D, 0x14, 0x20, 0x02, 0xA1, 0xC8, 0xDA, 0x12,
    0x81, 0x7F, 0xD9, 0xF1, 0xDB, 0x40, 0xA4, 0xA6,
    0x2E, 0xAF
  };
  uint32_t index = 0U;

  s_oled_ready = false;
  s_oled_addr = 0U;

  if (drv_oled_probe_address(DRV_OLED_ADDR_PRIMARY) == HAL_OK)
  {
    s_oled_addr = DRV_OLED_ADDR_PRIMARY;
    s_oled_ready = true;
  }
  else if (drv_oled_probe_address(DRV_OLED_ADDR_SECONDARY) == HAL_OK)
  {
    s_oled_addr = DRV_OLED_ADDR_SECONDARY;
    s_oled_ready = true;
  }

  if (!s_oled_ready)
  {
    return HAL_ERROR;
  }

  s_oled_ready = false;

  for (index = 0U; index < (sizeof(init_commands) / sizeof(init_commands[0])); index++)
  {
    if (drv_oled_write_command(init_commands[index]) != HAL_OK)
    {
      s_oled_addr = 0U;
      return HAL_ERROR;
    }
  }

  s_oled_ready = true;
  if (drv_oled_clear() != HAL_OK)
  {
    s_oled_ready = false;
    s_oled_addr = 0U;
    return HAL_ERROR;
  }

  return HAL_OK;
}

HAL_StatusTypeDef drv_oled_clear(void)
{
  uint8_t page = 0U;
  uint8_t chunk = 0U;

  if (!s_oled_ready)
  {
    return HAL_ERROR;
  }

  for (page = 0U; page < DRV_OLED_PAGE_COUNT; page++)
  {
    if (drv_oled_set_cursor(0U, page) != HAL_OK)
    {
      return HAL_ERROR;
    }

    for (chunk = 0U; chunk < (DRV_OLED_WIDTH / DRV_OLED_DATA_CHUNK_SIZE); chunk++)
    {
      if (drv_oled_write_data(s_oled_zero_chunk, DRV_OLED_DATA_CHUNK_SIZE) != HAL_OK)
      {
        return HAL_ERROR;
      }
    }
  }

  return HAL_OK;
}

HAL_StatusTypeDef drv_oled_show_string(uint8_t x, uint8_t y, const char *str)
{
  uint8_t glyph_buffer[DRV_OLED_CHAR_WIDTH] = {0};
  uint8_t column = x;
  const uint8_t *glyph = NULL;

  if ((!s_oled_ready) || (str == NULL) || (x >= DRV_OLED_WIDTH) || (y >= DRV_OLED_PAGE_COUNT))
  {
    return HAL_ERROR;
  }

  if (drv_oled_set_cursor(x, y) != HAL_OK)
  {
    return HAL_ERROR;
  }

  while ((*str != '\0') && ((uint16_t)column + DRV_OLED_CHAR_WIDTH) <= DRV_OLED_WIDTH)
  {
    glyph = drv_oled_get_glyph(*str);
    (void)memcpy(glyph_buffer, glyph, DRV_OLED_GLYPH_WIDTH);
    glyph_buffer[DRV_OLED_GLYPH_WIDTH] = 0x00U;

    if (drv_oled_write_data(glyph_buffer, sizeof(glyph_buffer)) != HAL_OK)
    {
      return HAL_ERROR;
    }

    column = (uint8_t)(column + DRV_OLED_CHAR_WIDTH);
    str++;
  }

  return HAL_OK;
}

HAL_StatusTypeDef drv_oled_show_boot_page(void)
{
  if (drv_oled_clear() != HAL_OK)
  {
    return HAL_ERROR;
  }

  if (drv_oled_show_string(0U, 0U, "T/H METER") != HAL_OK)
  {
    return HAL_ERROR;
  }

  if (drv_oled_show_string(0U, 2U, "BOOT") != HAL_OK)
  {
    return HAL_ERROR;
  }

  return drv_oled_show_string(0U, 4U, APP_VERSION_STR);
}

HAL_StatusTypeDef drv_oled_show_hw_check_page(uint8_t i2c1_device_count,
                                              uint8_t i2c2_device_count,
                                              uint8_t oled_ready,
                                              uint8_t ads_ready,
                                              uint8_t pwm_has_valid_sample)
{
  char line_buffer[DRV_OLED_TEXT_BUFFER_SIZE] = {0};

  if (drv_oled_clear() != HAL_OK)
  {
    return HAL_ERROR;
  }

  if (drv_oled_show_string(0U, 0U, "HW CHECK") != HAL_OK)
  {
    return HAL_ERROR;
  }

  (void)snprintf(line_buffer,
                 sizeof(line_buffer),
                 "I1:%u I2:%u",
                 (unsigned int)i2c1_device_count,
                 (unsigned int)i2c2_device_count);
  if (drv_oled_show_string(0U, 2U, line_buffer) != HAL_OK)
  {
    return HAL_ERROR;
  }

  (void)snprintf(line_buffer,
                 sizeof(line_buffer),
                 "ADS:%s OLED:%s",
                 (ads_ready != 0U) ? "OK" : "NO",
                 (oled_ready != 0U) ? "OK" : "NO");
  if (drv_oled_show_string(0U, 4U, line_buffer) != HAL_OK)
  {
    return HAL_ERROR;
  }

  (void)snprintf(line_buffer,
                 sizeof(line_buffer),
                 "PWM:%s",
                 (pwm_has_valid_sample != 0U) ? "OK" : "WAIT");
  return drv_oled_show_string(0U, 6U, line_buffer);
}

HAL_StatusTypeDef drv_oled_show_voltage_page(float v_ntc,
                                             float v_chs1101,
                                             const char *ntc_state_short,
                                             const char *rh_state_short)
{
  char line_buffer[DRV_OLED_TEXT_BUFFER_SIZE] = {0};
  const char *resolved_ntc_state = drv_oled_resolve_short_state(ntc_state_short, "SKIP");
  const char *resolved_rh_state = drv_oled_resolve_short_state(rh_state_short, "SKIP");

  if (drv_oled_clear() != HAL_OK)
  {
    return HAL_ERROR;
  }

  if (drv_oled_show_string(0U, 0U, "ADC VOLT") != HAL_OK)
  {
    return HAL_ERROR;
  }

  drv_oled_format_value(line_buffer, sizeof(line_buffer), "V0:", v_ntc, 3U, "V");
  if (drv_oled_show_string(0U, 2U, line_buffer) != HAL_OK)
  {
    return HAL_ERROR;
  }

  drv_oled_format_value(line_buffer, sizeof(line_buffer), "V1:", v_chs1101, 3U, "V");
  if (drv_oled_show_string(0U, 4U, line_buffer) != HAL_OK)
  {
    return HAL_ERROR;
  }

  (void)snprintf(line_buffer,
                 sizeof(line_buffer),
                 "A0:%.5s A1:%.5s",
                 resolved_ntc_state,
                 resolved_rh_state);
  return drv_oled_show_string(0U, 6U, line_buffer);
}

HAL_StatusTypeDef drv_oled_show_calc_page(float temp_c,
                                          const char *ntc_state_short,
                                          float rh_adc,
                                          float rh_pwm,
                                          const char *pwm_state_short)
{
  char line_buffer[DRV_OLED_TEXT_BUFFER_SIZE] = {0};
  char rh_buffer[DRV_OLED_TEXT_BUFFER_SIZE] = {0};
  const char *resolved_ntc_state = drv_oled_resolve_short_state(ntc_state_short, "SKIP");
  const char *resolved_pwm_state = drv_oled_resolve_short_state(pwm_state_short, "SKIP");

  (void)rh_pwm;

  if (drv_oled_clear() != HAL_OK)
  {
    return HAL_ERROR;
  }

  if (drv_oled_show_string(0U, 0U, "CALC") != HAL_OK)
  {
    return HAL_ERROR;
  }

  drv_oled_format_value(line_buffer, sizeof(line_buffer), "T:", temp_c, 1U, "C");
  if (drv_oled_show_string(0U, 2U, line_buffer) != HAL_OK)
  {
    return HAL_ERROR;
  }

  drv_oled_format_value(rh_buffer, sizeof(rh_buffer), "RH:", rh_adc, 1U, "%");
  (void)snprintf(line_buffer,
                 sizeof(line_buffer),
                 "T:%.5s %.10s",
                 resolved_ntc_state,
                 rh_buffer);
  if (drv_oled_show_string(0U, 4U, line_buffer) != HAL_OK)
  {
    return HAL_ERROR;
  }

  (void)snprintf(line_buffer, sizeof(line_buffer), "PWM:%.5s", resolved_pwm_state);
  return drv_oled_show_string(0U, 6U, line_buffer);
}

HAL_StatusTypeDef drv_oled_show_measure_page(float temp_c, float rh_pwm, float rh_adc)
{
  char line_buffer[DRV_OLED_TEXT_BUFFER_SIZE] = {0};

  if (drv_oled_clear() != HAL_OK)
  {
    return HAL_ERROR;
  }

  drv_oled_format_value(line_buffer, sizeof(line_buffer), "TEMP:", temp_c, 2U, "C");
  if (drv_oled_show_string(0U, 0U, line_buffer) != HAL_OK)
  {
    return HAL_ERROR;
  }

  drv_oled_format_value(line_buffer, sizeof(line_buffer), "RH_PWM:", rh_pwm, 2U, "%");
  if (drv_oled_show_string(0U, 2U, line_buffer) != HAL_OK)
  {
    return HAL_ERROR;
  }

  drv_oled_format_value(line_buffer, sizeof(line_buffer), "RH_ADC:", rh_adc, 2U, "%");
  return drv_oled_show_string(0U, 4U, line_buffer);
}

HAL_StatusTypeDef drv_oled_show_diag_page(uint8_t i2c1_devices,
                                          uint8_t i2c2_devices,
                                          uint8_t oled_ready,
                                          uint8_t ads_ready,
                                          const char *ch0_state,
                                          uint16_t ch0_error_count,
                                          const char *ch1_state,
                                          uint16_t ch1_error_count)
{
  char line_buffer[DRV_OLED_TEXT_BUFFER_SIZE] = {0};

  if (drv_oled_clear() != HAL_OK)
  {
    return HAL_ERROR;
  }

  (void)snprintf(line_buffer,
                 sizeof(line_buffer),
                 "I2C1:%u I2C2:%u",
                 (unsigned int)i2c1_devices,
                 (unsigned int)i2c2_devices);
  if (drv_oled_show_string(0U, 0U, line_buffer) != HAL_OK)
  {
    return HAL_ERROR;
  }

  (void)snprintf(line_buffer,
                 sizeof(line_buffer),
                 "OLED:%u ADS:%s",
                 (unsigned int)((oled_ready != 0U) ? 1U : 0U),
                 (ads_ready != 0U) ? "OK" : "NO");
  if (drv_oled_show_string(0U, 2U, line_buffer) != HAL_OK)
  {
    return HAL_ERROR;
  }

  drv_oled_format_diag_channel(line_buffer, sizeof(line_buffer), 0U, ch0_state, ch0_error_count);
  if (drv_oled_show_string(0U, 4U, line_buffer) != HAL_OK)
  {
    return HAL_ERROR;
  }

  drv_oled_format_diag_channel(line_buffer, sizeof(line_buffer), 1U, ch1_state, ch1_error_count);
  return drv_oled_show_string(0U, 6U, line_buffer);
}
