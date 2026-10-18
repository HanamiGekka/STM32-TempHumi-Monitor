#include "drv_ads1115.h"

#include <stdbool.h>
#include <stddef.h>

/*
 * hi2c2 由 CubeMX 初始化；本模块只复用句柄，不改 I2C 外设配置。
 * 当前关键参数的来源分层如下：
 * - `0x48`、`AIN0 -> V_NTC`、`AIN1 -> V_CHS1101` 来自硬件固定事实；
 * - 单次转换、`±4.096 V`、`250 SPS`、`OS 位轮询 + 超时保护` 来自当前软件默认实现，
 *   目标是在 `1 s` 窗口内支撑默认 `16 groups/s` 的保守采样策略。
 */
extern I2C_HandleTypeDef hi2c2;

#define DRV_ADS1115_ADDR_7BIT            0x48U
#define DRV_ADS1115_REG_CONVERSION       0x00U
#define DRV_ADS1115_REG_CONFIG           0x01U

#define DRV_ADS1115_CFG_OS_SINGLE        0x8000U
#define DRV_ADS1115_CFG_OS_READY         0x8000U
#define DRV_ADS1115_CFG_MUX_AIN0_GND     0x4000U
#define DRV_ADS1115_CFG_MUX_AIN1_GND     0x5000U
#define DRV_ADS1115_CFG_MUX_AIN2_GND     0x6000U
#define DRV_ADS1115_CFG_MUX_AIN3_GND     0x7000U
#define DRV_ADS1115_CFG_PGA_4V096        0x0200U
#define DRV_ADS1115_CFG_MODE_SINGLE      0x0100U
#define DRV_ADS1115_CFG_DR_250SPS        0x00A0U
#define DRV_ADS1115_CFG_COMP_DISABLE     0x0003U

#define DRV_ADS1115_READY_TRIALS         2U
#define DRV_ADS1115_READY_TIMEOUT_MS     10U
/* 250 SPS 理论转换时间约 4 ms；轮询 OS 位时保留超时保护，避免异常硬件状态卡死主循环。 */
#define DRV_ADS1115_CONVERSION_TIMEOUT_MS       20U
#define DRV_ADS1115_CONVERSION_POLL_DELAY_MS    1U
/* 当前按 PGA=+-4.096 V 计算 LSB，便于直接把原始码值换算成工程电压。 */
#define DRV_ADS1115_LSB_VOLT             (4.096f / 32768.0f)

static bool s_ads1115_initialized = false;
static drv_ads1115_diag_reason_t s_ads1115_last_diag = DRV_ADS1115_DIAG_NONE;

static void drv_ads1115_set_last_diag(drv_ads1115_diag_reason_t reason)
{
  s_ads1115_last_diag = reason;
}

static uint16_t drv_ads1115_build_mux(uint8_t channel)
{
  /* 当前业务只用到 AIN0/AIN1；AIN2/AIN3 仍保留为底层能力枚举。 */
  switch (channel)
  {
    case DRV_ADS1115_CHANNEL_AIN0:
      return DRV_ADS1115_CFG_MUX_AIN0_GND;

    case DRV_ADS1115_CHANNEL_AIN1:
      return DRV_ADS1115_CFG_MUX_AIN1_GND;

    case DRV_ADS1115_CHANNEL_AIN2:
      return DRV_ADS1115_CFG_MUX_AIN2_GND;

    case DRV_ADS1115_CHANNEL_AIN3:
      return DRV_ADS1115_CFG_MUX_AIN3_GND;

    default:
      return 0U;
  }
}

static HAL_StatusTypeDef drv_ads1115_read_config(uint16_t *out_config)
{
  uint8_t rx_buffer[2] = {0};

  if (out_config == NULL)
  {
    return HAL_ERROR;
  }

  if (HAL_I2C_Mem_Read(&hi2c2,
                       ((uint16_t)DRV_ADS1115_ADDR_7BIT << 1),
                       DRV_ADS1115_REG_CONFIG,
                       I2C_MEMADD_SIZE_8BIT,
                       rx_buffer,
                       sizeof(rx_buffer),
                       DRV_ADS1115_READY_TIMEOUT_MS) != HAL_OK)
  {
    return HAL_ERROR;
  }

  *out_config = (uint16_t)((((uint16_t)rx_buffer[0]) << 8) | rx_buffer[1]);
  return HAL_OK;
}

static HAL_StatusTypeDef drv_ads1115_wait_conversion_ready(uint32_t timeout_ms)
{
  uint32_t start_tick = HAL_GetTick();
  uint16_t config_word = 0U;

  do
  {
    if (drv_ads1115_read_config(&config_word) != HAL_OK)
    {
      return HAL_ERROR;
    }

    if ((config_word & DRV_ADS1115_CFG_OS_READY) != 0U)
    {
      return HAL_OK;
    }

    HAL_Delay(DRV_ADS1115_CONVERSION_POLL_DELAY_MS);
  } while ((HAL_GetTick() - start_tick) < timeout_ms);

  return HAL_TIMEOUT;
}

HAL_StatusTypeDef drv_ads1115_init(void)
{
  /* 当前 init 只负责声明“软件已准备好使用该模块”，不主动写设备默认寄存器。 */
  s_ads1115_initialized = true;
  drv_ads1115_set_last_diag(DRV_ADS1115_DIAG_NONE);
  return HAL_OK;
}

HAL_StatusTypeDef drv_ads1115_probe(void)
{
  return HAL_I2C_IsDeviceReady(&hi2c2,
                               ((uint16_t)DRV_ADS1115_ADDR_7BIT << 1),
                               DRV_ADS1115_READY_TRIALS,
                               DRV_ADS1115_READY_TIMEOUT_MS);
}

HAL_StatusTypeDef drv_ads1115_read_raw(uint8_t channel, int16_t *out_raw)
{
  uint16_t config_word = 0U;
  HAL_StatusTypeDef wait_status = HAL_OK;
  uint8_t tx_buffer[2] = {0};
  uint8_t rx_buffer[2] = {0};

  if (!s_ads1115_initialized)
  {
    drv_ads1115_set_last_diag(DRV_ADS1115_DIAG_NOT_INITIALIZED);
    return HAL_ERROR;
  }

  if (out_raw == NULL)
  {
    drv_ads1115_set_last_diag(DRV_ADS1115_DIAG_NULL_ARG);
    return HAL_ERROR;
  }

  config_word = drv_ads1115_build_mux(channel);
  if (config_word == 0U)
  {
    drv_ads1115_set_last_diag(DRV_ADS1115_DIAG_INVALID_CHANNEL);
    return HAL_ERROR;
  }

  if (drv_ads1115_probe() != HAL_OK)
  {
    drv_ads1115_set_last_diag(DRV_ADS1115_DIAG_PROBE_FAIL);
    return HAL_ERROR;
  }

  /*
   * 当前每次读取都重新下发配置：
   * 1. 单次转换，避免连续模式残留状态影响 bring-up 判断；
   * 2. 单端对地读取，和当前两路电压链一致；
   * 3. `250 SPS` 下写配置后轮询 OS 位，避免 AIN0/AIN1 读到旧转换结果。
   */
  config_word |= DRV_ADS1115_CFG_OS_SINGLE;
  config_word |= DRV_ADS1115_CFG_PGA_4V096;
  config_word |= DRV_ADS1115_CFG_MODE_SINGLE;
  config_word |= DRV_ADS1115_CFG_DR_250SPS;
  config_word |= DRV_ADS1115_CFG_COMP_DISABLE;

  tx_buffer[0] = (uint8_t)(config_word >> 8);
  tx_buffer[1] = (uint8_t)(config_word & 0x00FFU);

  if (HAL_I2C_Mem_Write(&hi2c2,
                        ((uint16_t)DRV_ADS1115_ADDR_7BIT << 1),
                        DRV_ADS1115_REG_CONFIG,
                        I2C_MEMADD_SIZE_8BIT,
                        tx_buffer,
                        sizeof(tx_buffer),
                        DRV_ADS1115_READY_TIMEOUT_MS) != HAL_OK)
  {
    drv_ads1115_set_last_diag(DRV_ADS1115_DIAG_CONFIG_WRITE_FAIL);
    return HAL_ERROR;
  }

  wait_status = drv_ads1115_wait_conversion_ready(DRV_ADS1115_CONVERSION_TIMEOUT_MS);
  if (wait_status != HAL_OK)
  {
    drv_ads1115_set_last_diag((wait_status == HAL_TIMEOUT) ? DRV_ADS1115_DIAG_OS_TIMEOUT : DRV_ADS1115_DIAG_CONFIG_READ_FAIL);
    return wait_status;
  }

  if (HAL_I2C_Mem_Read(&hi2c2,
                       ((uint16_t)DRV_ADS1115_ADDR_7BIT << 1),
                       DRV_ADS1115_REG_CONVERSION,
                       I2C_MEMADD_SIZE_8BIT,
                       rx_buffer,
                       sizeof(rx_buffer),
                       DRV_ADS1115_READY_TIMEOUT_MS) != HAL_OK)
  {
    drv_ads1115_set_last_diag(DRV_ADS1115_DIAG_CONVERSION_READ_FAIL);
    return HAL_ERROR;
  }

  *out_raw = (int16_t)((((uint16_t)rx_buffer[0]) << 8) | rx_buffer[1]);
  drv_ads1115_set_last_diag(DRV_ADS1115_DIAG_NONE);
  return HAL_OK;
}

HAL_StatusTypeDef drv_ads1115_get_last_diag(drv_ads1115_diag_reason_t *out_reason)
{
  if (out_reason == NULL)
  {
    return HAL_ERROR;
  }

  *out_reason = s_ads1115_last_diag;
  return HAL_OK;
}

float drv_ads1115_raw_to_voltage(int16_t raw)
{
  return ((float)raw) * DRV_ADS1115_LSB_VOLT;
}
