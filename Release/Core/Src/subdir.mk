################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (13.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../Core/Src/algo_sensor_convert.c \
../Core/Src/app_command.c \
../Core/Src/app_hybrid_source.c \
../Core/Src/app_measurement_source.c \
../Core/Src/app_measurement_source_sim.c \
../Core/Src/app_protocol.c \
../Core/Src/app_th_meter.c \
../Core/Src/app_uart_command.c \
../Core/Src/bsp_i2c_scan.c \
../Core/Src/bsp_led_key.c \
../Core/Src/bsp_uart_command.c \
../Core/Src/bsp_uart_log.c \
../Core/Src/drv_ads1115.c \
../Core/Src/drv_humidity_pwm.c \
../Core/Src/drv_oled.c \
../Core/Src/main.c \
../Core/Src/stm32f1xx_hal_msp.c \
../Core/Src/stm32f1xx_it.c \
../Core/Src/syscalls.c \
../Core/Src/sysmem.c \
../Core/Src/system_stm32f1xx.c 

OBJS += \
./Core/Src/algo_sensor_convert.o \
./Core/Src/app_command.o \
./Core/Src/app_hybrid_source.o \
./Core/Src/app_measurement_source.o \
./Core/Src/app_measurement_source_sim.o \
./Core/Src/app_protocol.o \
./Core/Src/app_th_meter.o \
./Core/Src/app_uart_command.o \
./Core/Src/bsp_i2c_scan.o \
./Core/Src/bsp_led_key.o \
./Core/Src/bsp_uart_command.o \
./Core/Src/bsp_uart_log.o \
./Core/Src/drv_ads1115.o \
./Core/Src/drv_humidity_pwm.o \
./Core/Src/drv_oled.o \
./Core/Src/main.o \
./Core/Src/stm32f1xx_hal_msp.o \
./Core/Src/stm32f1xx_it.o \
./Core/Src/syscalls.o \
./Core/Src/sysmem.o \
./Core/Src/system_stm32f1xx.o 

C_DEPS += \
./Core/Src/algo_sensor_convert.d \
./Core/Src/app_command.d \
./Core/Src/app_hybrid_source.d \
./Core/Src/app_measurement_source.d \
./Core/Src/app_measurement_source_sim.d \
./Core/Src/app_protocol.d \
./Core/Src/app_th_meter.d \
./Core/Src/app_uart_command.d \
./Core/Src/bsp_i2c_scan.d \
./Core/Src/bsp_led_key.d \
./Core/Src/bsp_uart_command.d \
./Core/Src/bsp_uart_log.d \
./Core/Src/drv_ads1115.d \
./Core/Src/drv_humidity_pwm.d \
./Core/Src/drv_oled.d \
./Core/Src/main.d \
./Core/Src/stm32f1xx_hal_msp.d \
./Core/Src/stm32f1xx_it.d \
./Core/Src/syscalls.d \
./Core/Src/sysmem.d \
./Core/Src/system_stm32f1xx.d 


# Each subdirectory must supply rules for building sources it contributes
Core/Src/%.o Core/Src/%.su Core/Src/%.cyclo: ../Core/Src/%.c Core/Src/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m3 -std=gnu11 -DUSE_HAL_DRIVER -DSTM32F103xB -c -I../Core/Inc -I../Drivers/STM32F1xx_HAL_Driver/Inc/Legacy -I../Drivers/STM32F1xx_HAL_Driver/Inc -I../Drivers/CMSIS/Device/ST/STM32F1xx/Include -I../Drivers/CMSIS/Include -Os -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfloat-abi=soft -mthumb -o "$@"

clean: clean-Core-2f-Src

clean-Core-2f-Src:
	-$(RM) ./Core/Src/algo_sensor_convert.cyclo ./Core/Src/algo_sensor_convert.d ./Core/Src/algo_sensor_convert.o ./Core/Src/algo_sensor_convert.su ./Core/Src/app_command.cyclo ./Core/Src/app_command.d ./Core/Src/app_command.o ./Core/Src/app_command.su ./Core/Src/app_hybrid_source.cyclo ./Core/Src/app_hybrid_source.d ./Core/Src/app_hybrid_source.o ./Core/Src/app_hybrid_source.su ./Core/Src/app_measurement_source.cyclo ./Core/Src/app_measurement_source.d ./Core/Src/app_measurement_source.o ./Core/Src/app_measurement_source.su ./Core/Src/app_measurement_source_sim.cyclo ./Core/Src/app_measurement_source_sim.d ./Core/Src/app_measurement_source_sim.o ./Core/Src/app_measurement_source_sim.su ./Core/Src/app_protocol.cyclo ./Core/Src/app_protocol.d ./Core/Src/app_protocol.o ./Core/Src/app_protocol.su ./Core/Src/app_th_meter.cyclo ./Core/Src/app_th_meter.d ./Core/Src/app_th_meter.o ./Core/Src/app_th_meter.su ./Core/Src/app_uart_command.cyclo ./Core/Src/app_uart_command.d ./Core/Src/app_uart_command.o ./Core/Src/app_uart_command.su ./Core/Src/bsp_i2c_scan.cyclo ./Core/Src/bsp_i2c_scan.d ./Core/Src/bsp_i2c_scan.o ./Core/Src/bsp_i2c_scan.su ./Core/Src/bsp_led_key.cyclo ./Core/Src/bsp_led_key.d ./Core/Src/bsp_led_key.o ./Core/Src/bsp_led_key.su ./Core/Src/bsp_uart_command.cyclo ./Core/Src/bsp_uart_command.d ./Core/Src/bsp_uart_command.o ./Core/Src/bsp_uart_command.su ./Core/Src/bsp_uart_log.cyclo ./Core/Src/bsp_uart_log.d ./Core/Src/bsp_uart_log.o ./Core/Src/bsp_uart_log.su ./Core/Src/drv_ads1115.cyclo ./Core/Src/drv_ads1115.d ./Core/Src/drv_ads1115.o ./Core/Src/drv_ads1115.su ./Core/Src/drv_humidity_pwm.cyclo ./Core/Src/drv_humidity_pwm.d ./Core/Src/drv_humidity_pwm.o ./Core/Src/drv_humidity_pwm.su ./Core/Src/drv_oled.cyclo ./Core/Src/drv_oled.d ./Core/Src/drv_oled.o ./Core/Src/drv_oled.su ./Core/Src/main.cyclo ./Core/Src/main.d ./Core/Src/main.o ./Core/Src/main.su ./Core/Src/stm32f1xx_hal_msp.cyclo ./Core/Src/stm32f1xx_hal_msp.d ./Core/Src/stm32f1xx_hal_msp.o ./Core/Src/stm32f1xx_hal_msp.su ./Core/Src/stm32f1xx_it.cyclo ./Core/Src/stm32f1xx_it.d ./Core/Src/stm32f1xx_it.o ./Core/Src/stm32f1xx_it.su ./Core/Src/syscalls.cyclo ./Core/Src/syscalls.d ./Core/Src/syscalls.o ./Core/Src/syscalls.su ./Core/Src/sysmem.cyclo ./Core/Src/sysmem.d ./Core/Src/sysmem.o ./Core/Src/sysmem.su ./Core/Src/system_stm32f1xx.cyclo ./Core/Src/system_stm32f1xx.d ./Core/Src/system_stm32f1xx.o ./Core/Src/system_stm32f1xx.su

.PHONY: clean-Core-2f-Src
