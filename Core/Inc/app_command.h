#ifndef APP_COMMAND_H
#define APP_COMMAND_H
#include <stdint.h>
enum { APP_COMMAND_MAX_BYTES=63, APP_COMMAND_TIMEOUT_MS=1000 };
typedef enum {
  APP_COMMAND_NONE, APP_COMMAND_PING, APP_COMMAND_STATUS,
  APP_COMMAND_UNKNOWN, APP_COMMAND_OVERFLOW, APP_COMMAND_TIMEOUT, APP_COMMAND_BAD_BYTES,
  APP_COMMAND_MODE, APP_COMMAND_TEMP_REAL, APP_COMMAND_TEMP_SIM, APP_COMMAND_HUM_REAL, APP_COMMAND_HUM_SIM,
  APP_COMMAND_BOTH_RR, APP_COMMAND_BOTH_RS, APP_COMMAND_BOTH_SR, APP_COMMAND_BOTH_SS,
  APP_COMMAND_CAL_QUERY, APP_COMMAND_CAL_OFF, APP_COMMAND_CAL_AMBIENT,
  APP_COMMAND_CAL_LAD, APP_COMMAND_CAL_HUBER, APP_COMMAND_ID
} app_command_event_t;
typedef struct {
  char line[APP_COMMAND_MAX_BYTES+1];
  uint32_t last_byte_ms;
  uint8_t length;
  uint8_t discard;
  app_command_event_t fault;
} app_command_parser_t;
void app_command_reset_parser(app_command_parser_t *parser);
void app_command_discard_line(app_command_parser_t *parser);
app_command_event_t app_command_check_timeout(app_command_parser_t *parser,uint32_t now_ms);
app_command_event_t app_command_feed_byte(app_command_parser_t *parser,uint8_t byte,uint32_t now_ms);
#endif
