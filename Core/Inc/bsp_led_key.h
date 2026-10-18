#ifndef BSP_LED_KEY_H
#define BSP_LED_KEY_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>

#include "main.h"

void bsp_led_key_led_on(void);
void bsp_led_key_led_off(void);
void bsp_led_key_led_toggle(void);
GPIO_PinState bsp_led_key_read(void);
bool bsp_led_key_is_pressed(void);

#ifdef __cplusplus
}
#endif

#endif /* BSP_LED_KEY_H */
