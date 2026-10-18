#include "app_command.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static unsigned checks;
static app_command_event_t feed(app_command_parser_t *p,const char *s,uint32_t now) {
  app_command_event_t result=APP_COMMAND_NONE;
  while(*s) { app_command_event_t event=app_command_feed_byte(p,(uint8_t)*s++,now); if(event!=APP_COMMAND_NONE) result=event; }
  return result;
}
#define CHECK(value) do { assert(value); ++checks; } while(0)
int main(void) {
  app_command_parser_t p; app_command_reset_parser(&p);
  CHECK(feed(&p,"CMD,PI",10)==APP_COMMAND_NONE);
  CHECK(feed(&p,"NG\r\n",20)==APP_COMMAND_PING);
  CHECK(feed(&p,"CMD,STATUS?\n",30)==APP_COMMAND_STATUS);
  CHECK(feed(&p,"CMD,ID?\r\n",30)==APP_COMMAND_ID);
  CHECK(feed(&p,"CMD,ID\n",30)==APP_COMMAND_UNKNOWN);
  CHECK(feed(&p,"CMD,CAL?\n",30)==APP_COMMAND_CAL_QUERY);
  CHECK(feed(&p,"CMD,CAL,0\n",30)==APP_COMMAND_CAL_OFF);
  CHECK(feed(&p,"CMD,CAL,1\n",30)==APP_COMMAND_CAL_AMBIENT);
  CHECK(feed(&p,"CMD,CAL,2\r\n",30)==APP_COMMAND_CAL_LAD);
  CHECK(feed(&p,"CMD,CAL,3\n",30)==APP_COMMAND_CAL_HUBER);
  CHECK(feed(&p,"CMD,CAL,4\n",30)==APP_COMMAND_UNKNOWN);
  CHECK(feed(&p,"CMD,CAL,-1\n",30)==APP_COMMAND_UNKNOWN);
  CHECK(feed(&p,"\r\n\n",40)==APP_COMMAND_NONE);
  CHECK(feed(&p,"CMD,SET,TEMP,SIM\n",50)==APP_COMMAND_TEMP_SIM);
  CHECK(feed(&p,"CMD,PING\rX\n",60)==APP_COMMAND_UNKNOWN);
  CHECK(feed(&p,"cmd,ping\n",70)==APP_COMMAND_UNKNOWN);
  char large[100]; memset(large,'X',99); large[99]=0;
  CHECK(feed(&p,large,80)==APP_COMMAND_NONE);
  CHECK(p.length<=APP_COMMAND_MAX_BYTES);
  CHECK(feed(&p,"\n",81)==APP_COMMAND_OVERFLOW);
  CHECK(feed(&p,"CMD,PING\n",90)==APP_COMMAND_PING);
  CHECK(feed(&p,"CMD,",0xFFFFFF00U)==APP_COMMAND_NONE);
  CHECK(app_command_check_timeout(&p,0x00000010U)==APP_COMMAND_NONE);
  CHECK(app_command_check_timeout(&p,0x00000400U)==APP_COMMAND_TIMEOUT);
  CHECK(feed(&p,"PING\n",0x401)==APP_COMMAND_NONE);
  CHECK(feed(&p,"CMD,PING\n",0x402)==APP_COMMAND_PING);
  CHECK(feed(&p,"CMD,",2000)==APP_COMMAND_NONE);
  CHECK(app_command_feed_byte(&p,0,2001)==APP_COMMAND_NONE);
  CHECK(feed(&p,"PING\n",2002)==APP_COMMAND_BAD_BYTES);
  CHECK(feed(&p,"CMD,STATUS?\n",2003)==APP_COMMAND_STATUS);
  app_command_discard_line(&p);
  CHECK(feed(&p,"CMD,PING\n",3000)==APP_COMMAND_NONE);
  CHECK(feed(&p,"CMD,PING\n",3001)==APP_COMMAND_PING);
  CHECK(feed(&p,"CMD,PING\nCMD,STATUS?\n",4000)==APP_COMMAND_STATUS);
  printf("{\"checks\":%u,\"pass\":true,\"classification\":\"HOST FIXED VECTORS\"}\n",checks);
  return 0;
}
