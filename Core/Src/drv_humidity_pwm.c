#include "drv_humidity_pwm.h"

#include <stdbool.h>
#include <stddef.h>

/*
 * htim2 由 CubeMX 初始化；本模块只管理状态与样本，不改底层定时器配置。
 * 当前阶段只做到 “soft-ready”：
 * - 可以启动 TIM2_CH1 输入捕获；
 * - 可以保存未来真实回调写入的周期/高电平样本；
 * - 不会伪造 duty，避免把“无样本”误报成真实湿度输入。
 */
extern TIM_HandleTypeDef htim2;

static drv_humidity_pwm_status_t s_humidity_pwm_state = {0};

HAL_StatusTypeDef drv_humidity_pwm_init(void)
{
  s_humidity_pwm_state = (drv_humidity_pwm_status_t){0};
  s_humidity_pwm_state.initialized = true;
  return HAL_OK;
}

HAL_StatusTypeDef drv_humidity_pwm_start(void)
{
  if (!s_humidity_pwm_state.initialized)
  {
    return HAL_ERROR;
  }

  if (s_humidity_pwm_state.started)
  {
    return HAL_OK;
  }

  if (HAL_TIM_IC_Start(&htim2, TIM_CHANNEL_1) != HAL_OK)
  {
    return HAL_ERROR;
  }

  /* 启动成功并不等于已经拿到样本，后续仍可能处于 started 但 waiting sample 状态。 */
  s_humidity_pwm_state.started = true;
  s_humidity_pwm_state.has_valid_sample = false;
  s_humidity_pwm_state.last_period_ticks = 0U;
  s_humidity_pwm_state.last_high_ticks = 0U;
  s_humidity_pwm_state.last_duty = 0.0f;
  return HAL_OK;
}

HAL_StatusTypeDef drv_humidity_pwm_get_duty(float *out_duty)
{
  if (out_duty == NULL)
  {
    return HAL_ERROR;
  }

  *out_duty = 0.0f;

  /*
   * HAL_BUSY 在这里是有意设计的阶段语义：
   * - 可能模块还没初始化或没启动；
   * - 也可能已经 started，但还没有拿到任何真实捕获样本。
   * 上层遇到 HAL_BUSY 时应继续输出占位值，而不是伪造 0%RH。
   */
  if ((!s_humidity_pwm_state.initialized) || (!s_humidity_pwm_state.started) || (!s_humidity_pwm_state.has_valid_sample))
  {
    return HAL_BUSY;
  }

  *out_duty = s_humidity_pwm_state.last_duty;
  return HAL_OK;
}

HAL_StatusTypeDef drv_humidity_pwm_get_status(drv_humidity_pwm_status_t *out_status)
{
  if (out_status == NULL)
  {
    return HAL_ERROR;
  }

  *out_status = s_humidity_pwm_state;
  return HAL_OK;
}

HAL_StatusTypeDef drv_humidity_pwm_update_capture_sample(uint32_t period_ticks, uint32_t high_ticks)
{
  if ((!s_humidity_pwm_state.initialized) || (!s_humidity_pwm_state.started))
  {
    return HAL_ERROR;
  }

  if ((period_ticks == 0U) || (high_ticks > period_ticks))
  {
    return HAL_ERROR;
  }

  /*
   * 这里是未来真实捕获回调的唯一稳定写入口。
   * 业务层只关心“有没有 valid duty”，不关心具体来自哪种捕获实现；
   * 只要这里还没被真实样本调用过，就保持 `has_valid_sample=false`。
   */
  s_humidity_pwm_state.last_period_ticks = period_ticks;
  s_humidity_pwm_state.last_high_ticks = high_ticks;
  s_humidity_pwm_state.last_duty = (float)high_ticks / (float)period_ticks;
  s_humidity_pwm_state.has_valid_sample = true;

  return HAL_OK;
}
