#include "bsp_led_key.h"

#define BSP_LED_KEY_LED_GPIO_PORT GPIOC
#define BSP_LED_KEY_LED_PIN       GPIO_PIN_13

#define BSP_LED_KEY_GPIO_PORT     GPIOA
#define BSP_LED_KEY_PIN           GPIO_PIN_6

void bsp_led_key_led_on(void)
{
  /* PC13 为低电平点亮。 */
  HAL_GPIO_WritePin(BSP_LED_KEY_LED_GPIO_PORT, BSP_LED_KEY_LED_PIN, GPIO_PIN_RESET);
}

void bsp_led_key_led_off(void)
{
  HAL_GPIO_WritePin(BSP_LED_KEY_LED_GPIO_PORT, BSP_LED_KEY_LED_PIN, GPIO_PIN_SET);
}

void bsp_led_key_led_toggle(void)
{
  HAL_GPIO_TogglePin(BSP_LED_KEY_LED_GPIO_PORT, BSP_LED_KEY_LED_PIN);
}

GPIO_PinState bsp_led_key_read(void)
{
  return HAL_GPIO_ReadPin(BSP_LED_KEY_GPIO_PORT, BSP_LED_KEY_PIN);
}

bool bsp_led_key_is_pressed(void)
{
  /* PA6 使用上拉输入，按下时被拉低。 */
  return (bsp_led_key_read() == GPIO_PIN_RESET);
}
