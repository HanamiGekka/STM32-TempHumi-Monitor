#include "app_uart_command.h"
#include "app_command.h"
#include "bsp_uart_command.h"
#include "bsp_uart_log.h"
#include "app_version.h"
#include "app_protocol.h"
#include "app_th_meter.h"
#include <stdio.h>
static app_command_parser_t s_parser;
static uint32_t s_last_error_ms;
static bool s_error_sent;
static void app_uart_command_reply(app_command_event_t event) {
  const char *reply=0;
  char status[160];
  if(event==APP_COMMAND_NONE) return;
  if(event==APP_COMMAND_PING) reply="ACK,PING\r\n";
  else if(event==APP_COMMAND_ID) {
    uint8_t uid[12];uint16_t flash_kib,device_id;
    bsp_uart_command_read_identity(uid,&flash_kib,&device_id);
    (void)app_protocol_format_identity(status,sizeof(status),uid,flash_kib,device_id);reply=status;
  } else if(event==APP_COMMAND_STATUS) {
    const app_hybrid_mode_t *mode=app_th_meter_get_mode();
    const char *source=mode->effective_temperature!=mode->effective_humidity?"MIXED":
      (mode->effective_temperature==APP_MEASUREMENT_SOURCE_REAL?"REAL":"SIM");
    (void)snprintf(status,sizeof(status),"RSP,STATUS,1,%s,%lu,%s,0\r\n",APP_VERSION_STR,(unsigned long)HAL_GetTick(),source);
    reply=status;
  } else if(event>=APP_COMMAND_CAL_QUERY && event<=APP_COMMAND_CAL_HUBER) {
    if(event==APP_COMMAND_CAL_QUERY) {
      (void)snprintf(status,sizeof(status),"RSP,CAL,1,%u,%u\r\n",
        (unsigned)app_th_meter_requested_calibration(),(unsigned)app_th_meter_effective_calibration());
    } else {
      uint16_t profile=(uint16_t)(event-APP_COMMAND_CAL_OFF);
      if(!app_th_meter_request_calibration(profile)) reply="ERR,CAL_UNAVAILABLE\r\n";
      else (void)snprintf(status,sizeof(status),"ACK,CAL,%u\r\n",(unsigned)profile);
    }
    if(reply==0) reply=status;
  } else if(event==APP_COMMAND_MODE || (event>=APP_COMMAND_TEMP_REAL && event<=APP_COMMAND_BOTH_SS)) {
    const app_hybrid_mode_t *mode=app_th_meter_get_mode();
    if(event==APP_COMMAND_MODE) {
      (void)snprintf(status,sizeof(status),"RSP,MODE,1,%lu,%s,%s,%lu,%s,%s\r\n",
        (unsigned long)mode->requested_generation,mode->requested_temperature==APP_MEASUREMENT_SOURCE_REAL?"REAL":"SIM",
        mode->requested_humidity==APP_MEASUREMENT_SOURCE_REAL?"REAL":"SIM",
        (unsigned long)mode->effective_generation,mode->effective_temperature==APP_MEASUREMENT_SOURCE_REAL?"REAL":"SIM",
        mode->effective_humidity==APP_MEASUREMENT_SOURCE_REAL?"REAL":"SIM");
      reply=status;
    } else {
      app_measurement_source_type_t temperature=mode->requested_temperature,humidity=mode->requested_humidity;
      if(event==APP_COMMAND_TEMP_REAL) temperature=APP_MEASUREMENT_SOURCE_REAL;
      else if(event==APP_COMMAND_TEMP_SIM) temperature=APP_MEASUREMENT_SOURCE_SIMULATION;
      else if(event==APP_COMMAND_HUM_REAL) humidity=APP_MEASUREMENT_SOURCE_REAL;
      else if(event==APP_COMMAND_HUM_SIM) humidity=APP_MEASUREMENT_SOURCE_SIMULATION;
      else { temperature=(event==APP_COMMAND_BOTH_RR || event==APP_COMMAND_BOTH_RS)?APP_MEASUREMENT_SOURCE_REAL:APP_MEASUREMENT_SOURCE_SIMULATION;
        humidity=(event==APP_COMMAND_BOTH_RR || event==APP_COMMAND_BOTH_SR)?APP_MEASUREMENT_SOURCE_REAL:APP_MEASUREMENT_SOURCE_SIMULATION; }
      if(!app_th_meter_request_mode(temperature,humidity)) reply="ERR,SOURCE_UNAVAILABLE\r\n";
      else {
        (void)snprintf(status,sizeof(status),"ACK,SET,%lu,%s,%s\r\n",(unsigned long)mode->requested_generation,
          temperature==APP_MEASUREMENT_SOURCE_REAL?"REAL":"SIM",humidity==APP_MEASUREMENT_SOURCE_REAL?"REAL":"SIM");
        reply=status;
      }
    }
  } else {
    /* Malformed-input errors are limited to one/s; valid requests use bounded
       host stop-and-wait. Replies and existing logs share main-loop TX only. */
    if(s_error_sent && HAL_GetTick()-s_last_error_ms<1000U) return;
    s_error_sent=true; s_last_error_ms=HAL_GetTick();
    if(event==APP_COMMAND_OVERFLOW) reply="ERR,LINE_TOO_LONG\r\n";
    else if(event==APP_COMMAND_TIMEOUT) reply="ERR,COMMAND_TIMEOUT\r\n";
    else if(event==APP_COMMAND_BAD_BYTES) reply="ERR,BAD_BYTES\r\n";
    else reply="ERR,UNKNOWN_COMMAND\r\n";
  }
  (void)bsp_uart_log_write(reply);
}
void app_uart_command_init(UART_HandleTypeDef *uart) {
  app_command_reset_parser(&s_parser); s_error_sent=false;
  if(bsp_uart_command_init(uart)!=HAL_OK) (void)bsp_uart_log_write("ERR,RX_INIT\r\n");
}
void app_uart_command_loop(void) {
  uint8_t byte;
  uint32_t fault=bsp_uart_command_take_fault();
  if(fault!=0U) {
    char error[48]; app_command_discard_line(&s_parser);
    if(!s_error_sent || HAL_GetTick()-s_last_error_ms>=1000U) {
      s_error_sent=true; s_last_error_ms=HAL_GetTick();
      (void)snprintf(error,sizeof(error),"ERR,UART_RX,%lu\r\n",(unsigned long)fault);
      (void)bsp_uart_log_write(error);
    }
  }
  app_uart_command_reply(app_command_check_timeout(&s_parser,HAL_GetTick()));
  /* Limit work to 64 queued bytes per scheduler turn. */
  for(unsigned int count=0;count<64U && bsp_uart_command_read_byte(&byte);++count)
    app_uart_command_reply(app_command_feed_byte(&s_parser,byte,HAL_GetTick()));
}
