#include "app_command.h"
#include <string.h>
void app_command_reset_parser(app_command_parser_t *parser) { memset(parser,0,sizeof(*parser)); }
void app_command_discard_line(app_command_parser_t *parser) {
  app_command_reset_parser(parser); parser->discard=1U;
}
app_command_event_t app_command_check_timeout(app_command_parser_t *parser,uint32_t now_ms) {
  /* uint32 subtraction handles HAL tick wrap; a partial line expires at 1 s.
     Discard its suffix through LF, so a timed-out prefix can never execute. */
  if(parser->length>0U && !parser->discard && now_ms-parser->last_byte_ms>=APP_COMMAND_TIMEOUT_MS) {
    app_command_discard_line(parser); return APP_COMMAND_TIMEOUT;
  }
  return APP_COMMAND_NONE;
}
app_command_event_t app_command_feed_byte(app_command_parser_t *parser,uint8_t byte,uint32_t now_ms) {
  if(byte=='\n') {
    app_command_event_t result=parser->fault;
    if(!parser->discard) {
      if(parser->length>0U && parser->line[parser->length-1U]=='\r') --parser->length;
      parser->line[parser->length]='\0';
      if(parser->length==0U) result=APP_COMMAND_NONE;
      else if(strcmp(parser->line,"CMD,PING")==0) result=APP_COMMAND_PING;
      else if(strcmp(parser->line,"CMD,ID?")==0) result=APP_COMMAND_ID;
      else if(strcmp(parser->line,"CMD,STATUS?")==0) result=APP_COMMAND_STATUS;
      else if(strcmp(parser->line,"CMD,MODE?")==0) result=APP_COMMAND_MODE;
      else if(strcmp(parser->line,"CMD,CAL?")==0) result=APP_COMMAND_CAL_QUERY;
      else if(strcmp(parser->line,"CMD,CAL,0")==0) result=APP_COMMAND_CAL_OFF;
      else if(strcmp(parser->line,"CMD,CAL,1")==0) result=APP_COMMAND_CAL_AMBIENT;
      else if(strcmp(parser->line,"CMD,CAL,2")==0) result=APP_COMMAND_CAL_LAD;
      else if(strcmp(parser->line,"CMD,CAL,3")==0) result=APP_COMMAND_CAL_HUBER;
      else if(strcmp(parser->line,"CMD,SET,TEMP,REAL")==0) result=APP_COMMAND_TEMP_REAL;
      else if(strcmp(parser->line,"CMD,SET,TEMP,SIM")==0) result=APP_COMMAND_TEMP_SIM;
      else if(strcmp(parser->line,"CMD,SET,HUM,REAL")==0) result=APP_COMMAND_HUM_REAL;
      else if(strcmp(parser->line,"CMD,SET,HUM,SIM")==0) result=APP_COMMAND_HUM_SIM;
      else if(strcmp(parser->line,"CMD,SET,BOTH,REAL,REAL")==0) result=APP_COMMAND_BOTH_RR;
      else if(strcmp(parser->line,"CMD,SET,BOTH,REAL,SIM")==0) result=APP_COMMAND_BOTH_RS;
      else if(strcmp(parser->line,"CMD,SET,BOTH,SIM,REAL")==0) result=APP_COMMAND_BOTH_SR;
      else if(strcmp(parser->line,"CMD,SET,BOTH,SIM,SIM")==0) result=APP_COMMAND_BOTH_SS;
      else result=APP_COMMAND_UNKNOWN;
    }
    app_command_reset_parser(parser); return result;
  }
  if(parser->discard) return APP_COMMAND_NONE;
  parser->last_byte_ms=now_ms;
  if(byte!='\r' && (byte<32U || byte>126U)) {
    parser->discard=1U; parser->fault=APP_COMMAND_BAD_BYTES; return APP_COMMAND_NONE;
  }
  if(parser->length>=APP_COMMAND_MAX_BYTES) {
    parser->discard=1U; parser->fault=APP_COMMAND_OVERFLOW; return APP_COMMAND_NONE;
  }
  parser->line[parser->length++]=(char)byte; return APP_COMMAND_NONE;
}
