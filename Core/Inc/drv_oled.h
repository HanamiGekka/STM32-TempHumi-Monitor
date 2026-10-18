#ifndef DRV_OLED_H
#define DRV_OLED_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

HAL_StatusTypeDef drv_oled_init(void);
HAL_StatusTypeDef drv_oled_clear(void);
HAL_StatusTypeDef drv_oled_show_string(uint8_t x, uint8_t y, const char *str);
HAL_StatusTypeDef drv_oled_show_boot_page(void);
HAL_StatusTypeDef drv_oled_show_hw_check_page(uint8_t i2c1_device_count,
                                              uint8_t i2c2_device_count,
                                              uint8_t oled_ready,
                                              uint8_t ads_ready,
                                              uint8_t pwm_has_valid_sample);
HAL_StatusTypeDef drv_oled_show_voltage_page(float v_ntc,
                                             float v_chs1101,
                                             const char *ntc_state_short,
                                             const char *rh_state_short);
HAL_StatusTypeDef drv_oled_show_calc_page(float temp_c,
                                          const char *ntc_state_short,
                                          float rh_adc,
                                          float rh_pwm,
                                          const char *pwm_state_short);
HAL_StatusTypeDef drv_oled_show_measure_page(float temp_c, float rh_pwm, float rh_adc);
HAL_StatusTypeDef drv_oled_show_diag_page(uint8_t i2c1_devices,
                                          uint8_t i2c2_devices,
                                          uint8_t oled_ready,
                                          uint8_t ads_ready,
                                          const char *ch0_state,
                                          uint16_t ch0_error_count,
                                          const char *ch1_state,
                                          uint16_t ch1_error_count);

#ifdef __cplusplus
}
#endif

#endif /* DRV_OLED_H */
