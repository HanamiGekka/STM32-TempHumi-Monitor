#include "app_hybrid_source.h"
#include <string.h>
#include "app_measurement_source_sim.h"
#include "app_protocol.h"
#include "app_command.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
static unsigned checks;
#define CHECK(value) do { assert(value); ++checks; } while(0)
int main(void) {
  measurement_data_t real,sim,out;
  app_measurement_source_reset_data(&real,APP_MEASUREMENT_SOURCE_REAL,1000);
  real.temperature_c=24.39f; real.v_ntc=1.477f; real.v_chs1101=1.234f;
  real.raw_ntc_code=11817; real.raw_chs1101_code=9851;
  real.valid_flags=APP_MEASUREMENT_VALID_TEMPERATURE|APP_MEASUREMENT_VALID_V_NTC|APP_MEASUREMENT_VALID_V_CHS1101|APP_MEASUREMENT_VALID_RAW_NTC|APP_MEASUREMENT_VALID_RAW_CHS1101;
  real.error_flags=APP_MEASUREMENT_ERROR_CHS_RANGE|APP_MEASUREMENT_ERROR_PWM_NO_SAMPLE;
  real.valid=false; real.sample_count=1;
  app_measurement_source_sim.init(0);
  app_measurement_source_read(&app_measurement_source_sim,&sim,1000);
  app_hybrid_mode_t mode; app_hybrid_init_mode(&mode,APP_MEASUREMENT_SOURCE_REAL);
  CHECK(!mode.channel_selection_applied && mode.effective_temperature==APP_MEASUREMENT_SOURCE_REAL);
  CHECK(!app_hybrid_request_mode(&mode,APP_MEASUREMENT_SOURCE_REAL,APP_MEASUREMENT_SOURCE_SIMULATION,false,true));
  CHECK(!mode.pending && mode.requested_generation==0);
  CHECK(!app_hybrid_request_mode(&mode,APP_MEASUREMENT_SOURCE_REAL,APP_MEASUREMENT_SOURCE_REAL,true,false));
  CHECK(!app_hybrid_request_mode(&mode,APP_MEASUREMENT_SOURCE_MIXED,APP_MEASUREMENT_SOURCE_REAL,true,true));
  FILE *fixture=fopen("th3_vectors.txt","wb"); CHECK(fixture!=NULL);
  const app_measurement_source_type_t temperatures[]={APP_MEASUREMENT_SOURCE_REAL,APP_MEASUREMENT_SOURCE_REAL,APP_MEASUREMENT_SOURCE_SIMULATION,APP_MEASUREMENT_SOURCE_SIMULATION};
  const app_measurement_source_type_t humidities[]={APP_MEASUREMENT_SOURCE_REAL,APP_MEASUREMENT_SOURCE_SIMULATION,APP_MEASUREMENT_SOURCE_REAL,APP_MEASUREMENT_SOURCE_SIMULATION};
  for(unsigned i=0;i<4;++i) {
    uint32_t before=mode.effective_generation;
    CHECK(app_hybrid_request_mode(&mode,temperatures[i],humidities[i],true,true));
    CHECK(mode.effective_generation==before && mode.pending);
    CHECK(app_hybrid_apply_mode(&mode)); CHECK(!mode.pending && mode.effective_generation==i+1);
    app_hybrid_compose_channels(&out,&real,&sim,&mode,1000+i*1000);
    CHECK((out.valid_flags&APP_MEASUREMENT_VALID_TEMPERATURE)!=0);
    CHECK(out.valid==(humidities[i]==APP_MEASUREMENT_SOURCE_SIMULATION));
    if(temperatures[i]==APP_MEASUREMENT_SOURCE_SIMULATION) CHECK(isnan(out.v_ntc) && !(out.valid_flags&APP_MEASUREMENT_VALID_RAW_NTC));
    else CHECK(out.raw_ntc_code==11817 && out.v_ntc==real.v_ntc);
    if(humidities[i]==APP_MEASUREMENT_SOURCE_SIMULATION) CHECK(isnan(out.v_chs1101) && !(out.valid_flags&APP_MEASUREMENT_VALID_RAW_CHS1101) && out.error_flags==0);
    else CHECK(out.raw_chs1101_code==9851 && out.error_flags==48 && !out.valid);
    char line[256]; int n=app_protocol_format_measurement_v3(line,sizeof(line),&out,temperatures[i],humidities[i],mode.effective_generation);
    CHECK(n>0); CHECK(fwrite(line,1,(size_t)n,fixture)==(size_t)n);
    CHECK(app_protocol_format_measurement_v3(line,8,&out,temperatures[i],humidities[i],mode.effective_generation)==-1);
    CHECK(app_protocol_format_measurement_v3(line,sizeof(line),&out,APP_MEASUREMENT_SOURCE_MIXED,humidities[i],mode.effective_generation)==-1);
  }
  CHECK(fclose(fixture)==0);
  CHECK(app_hybrid_request_mode(&mode,APP_MEASUREMENT_SOURCE_REAL,APP_MEASUREMENT_SOURCE_SIMULATION,true,true));
  CHECK(app_hybrid_apply_mode(&mode));
  app_hybrid_compose_channels(&out,NULL,&sim,&mode,5000);
  CHECK(!out.valid && (out.error_flags&APP_MEASUREMENT_ERROR_SOURCE_UNAVAILABLE) && (out.valid_flags&APP_MEASUREMENT_VALID_HUMIDITY));
  CHECK(isnan(out.temperature_c));
  char invalid_line[256];
  const uint8_t uid[12]={0x01,0x23,0x45,0x67,0x89,0xab,0xcd,0xef,0x10,0x32,0x54,0x76};
  CHECK(app_protocol_format_identity(invalid_line,sizeof(invalid_line),uid,64,0x410)>0);
  CHECK(strcmp(invalid_line,"RSP,ID,1,STM32F1,0123456789abcdef10325476,64,1040\r\n")==0);
  CHECK(app_protocol_format_identity(invalid_line,sizeof(invalid_line),uid,64,0)>0);
  CHECK(strcmp(invalid_line,"RSP,ID,1,STM32F1,0123456789abcdef10325476,64,0\r\n")==0);
  CHECK(app_protocol_format_identity(invalid_line,8,uid,64,0x410)==-1);
  CHECK(app_protocol_format_identity(NULL,0,uid,64,0x410)==-1);
  CHECK(app_protocol_format_identity(invalid_line,sizeof(invalid_line),NULL,64,0x410)==-1);
  CHECK(app_protocol_format_measurement_v3(invalid_line,sizeof(invalid_line),&out,APP_MEASUREMENT_SOURCE_REAL,APP_MEASUREMENT_SOURCE_SIMULATION,5)>0);
  out.source_type=APP_MEASUREMENT_SOURCE_REAL;
  CHECK(app_protocol_format_measurement_v3(invalid_line,sizeof(invalid_line),&out,APP_MEASUREMENT_SOURCE_REAL,APP_MEASUREMENT_SOURCE_SIMULATION,5)==-1);
  real.temperature_c=NAN;
  app_hybrid_compose_channels(&out,&real,&sim,&mode,5100);
  CHECK(!(out.valid_flags&APP_MEASUREMENT_VALID_TEMPERATURE) && !out.valid && (out.valid_flags&APP_MEASUREMENT_VALID_RAW_NTC));
  CHECK(out.raw_ntc_code==11817);
  mode.requested_generation=0xFFFFFFFFU;
  CHECK(app_hybrid_request_mode(&mode,APP_MEASUREMENT_SOURCE_REAL,APP_MEASUREMENT_SOURCE_REAL,true,true)); CHECK(mode.requested_generation==1);
  app_command_parser_t command; app_command_reset_parser(&command);
  const char *requests[]={"CMD,MODE?\n","CMD,SET,TEMP,SIM\n","CMD,SET,HUM,REAL\n","CMD,SET,BOTH,REAL,SIM\n","CMD,SET,TEMP,INVALID\n"};
  const app_command_event_t expected[]={APP_COMMAND_MODE,APP_COMMAND_TEMP_SIM,APP_COMMAND_HUM_REAL,APP_COMMAND_BOTH_RS,APP_COMMAND_UNKNOWN};
  for(unsigned i=0;i<5;++i) { app_command_event_t event=APP_COMMAND_NONE; for(const char *p=requests[i];*p;++p) event=app_command_feed_byte(&command,(uint8_t)*p,0); CHECK(event==expected[i]); }
  printf("{\"checks\":%u,\"pass\":true,\"classification\":\"HOST FIXED VECTORS ONLY\"}\n",checks);
  return 0;
}
