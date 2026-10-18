#include "algo_sensor_convert.h"
#include "app_protocol.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks = 0U, failures = 0U;
static void check(int condition) { ++checks; if (!condition) { ++failures; } }

int main(int argc, char **argv)
{
  uint16_t id = 99U;
  if (argc == 2)
  {
    float input = strtof(argv[1], NULL);
    float result = algo_temperature_correct_mean(input, true, &id);
    printf("%.9g %.9g %u\n", (double)input, (double)result, (unsigned)id);
    return 0;
  }
  if (argc == 3)
  {
    float input=strtof(argv[1],NULL);
    float value=algo_temperature_correct_profile(input,(uint16_t)atoi(argv[2]),&id);
    printf("%.9g %.9g %u\n",(double)input,(double)value,(unsigned)id);return 0;
  }
  for(uint16_t profile=0U;profile<=4U;++profile)
  {
    float previous=algo_temperature_correct_profile(-40.0f,profile,NULL);
    for(unsigned i=1;i<=330000U;++i)
    {
      float raw=-40.0f+0.0005f*(float)i;
      float value=algo_temperature_correct_profile(raw,profile,&id);
      check(isfinite(value) &&value>previous);previous=value;
      if(profile==0U ||profile>3U ||!ALGO_TEMPERATURE_CORRECTION_ENABLED ||raw<=25.2f ||raw>=35.6f)
        check(value==raw &&id==0U);
      else check(id==profile &&value>=raw &&value<raw+6.0f);
    }
    check(isnan(algo_temperature_correct_profile(NAN,profile,&id)) &&id==0U);
    check(isinf(algo_temperature_correct_profile(INFINITY,profile,&id)) &&id==0U);
  }
  const float values[] = {-40.0f, 0.0f, 24.0f, 25.2f, 25.4f, 25.7f,
                          26.0f, 30.0f, 32.6f, 34.2f, 35.6f, 50.0f, 125.0f};
  for (unsigned i=0U; i<sizeof(values)/sizeof(values[0]); ++i)
  {
    float raw=values[i], value=algo_temperature_correct_mean(raw,true,&id);
    check(isfinite(value)); check(value >= raw && value <= raw+0.90001f);
    check(algo_temperature_correct_mean(raw,false,&id)==raw && id==0U);
    value=algo_temperature_correct_mean(raw,true,&id);
    if (raw <= 25.2f || raw >= 35.6f || !ALGO_TEMPERATURE_CORRECTION_ENABLED)
      check(value==raw && id==0U);
    else check(id==1U);
  }
  check(isnan(algo_temperature_correct_mean(NAN,true,&id)) && id==0U);
  check(isinf(algo_temperature_correct_mean(INFINITY,true,&id)) && id==0U);
  check(isinf(algo_temperature_correct_mean(-INFINITY,true,&id)) && id==0U);
  check(isnan(algo_temperature_correct_mean(algo_ntc_voltage_to_temp_c(0.0f),true,&id)) && id==0U);
  check(isnan(algo_temperature_correct_mean(algo_ntc_voltage_to_temp_c(4.096f),true,&id)) && id==0U);
  float previous=algo_temperature_correct_mean(-40.0f,true,NULL);
  for (unsigned i=1U;i<=33000U;++i)
  {
    float raw=-40.0f+0.005f*(float)i;
    float value=algo_temperature_correct_mean(raw,true,&id);
    check(value>previous);previous=value;
  }
  const float boundaries[]={25.2f,25.7f,32.6f,35.6f};
  for (unsigned i=0U;i<4U;++i)
  {
    float x=boundaries[i];float l=algo_temperature_correct_mean(x-0.0001f,true,NULL);
    float c=algo_temperature_correct_mean(x,true,NULL),r=algo_temperature_correct_mean(x+0.0001f,true,NULL);
    check(l<c && c<r && r-l<0.001f);
  }
  /* Original physical model remains invertible at 50 C; no 35 C software cap. */
  float v50=algo_ntc_expected_voltage_from_temp_c(50.0f);
  check(isfinite(v50) && v50>0.0f && fabsf(algo_ntc_voltage_to_temp_c(v50)-50.0f)<0.002f);
  char diagnostic[224];float raw=26.125f,corr=algo_temperature_correct_mean(raw,true,&id);
  int length=app_protocol_format_temperature_correction(diagnostic,sizeof(diagnostic),raw,corr,id);
  check(length>0 && length<(int)sizeof(diagnostic));
  check(strstr(diagnostic,"raw=26.125C")!=NULL && strstr(diagnostic,"raw_bits=41D10000")!=NULL);
  check(app_protocol_format_temperature_correction(diagnostic,sizeof(diagnostic),NAN,corr,id)<0);
  length=app_protocol_format_temperature_evidence(diagnostic,sizeof(diagnostic),raw,corr,1U,UINT32_MAX,1U);
  check(length>0 &&strstr(diagnostic,"t_ms=4294967295 profile=1")!=NULL);
  check(app_protocol_format_temperature_evidence(diagnostic,32U,raw,corr,1U,0U,1U)<0);
  measurement_data_t data={0};data.temperature_c=corr;data.humidity_rh=50.0f;
  data.v_ntc=1.4f;data.raw_ntc_code=11200;data.valid_flags=71U;data.valid=true;
  data.calibration_id=id;data.sample_count=16U;data.source_type=APP_MEASUREMENT_SOURCE_MIXED;
  length=app_protocol_format_measurement_v3(diagnostic,sizeof(diagnostic),&data,
    APP_MEASUREMENT_SOURCE_REAL,APP_MEASUREMENT_SOURCE_SIMULATION,3U);
  check(length>0 && strstr(diagnostic,"TH3,MIXED,")==diagnostic);
  unsigned commas=0U;for(int i=0;i<length;++i){if(diagnostic[i]==','){++commas;}}
  check(commas==22U && strstr(diagnostic,"REAL,SIM,1,1,3")!=NULL);
  printf("{\"pass\":%s,\"checks\":%u,\"failures\":%u,\"compiled_enabled\":%u,\"v50\":%.9g}\n",
    failures==0U?"true":"false",checks,failures,ALGO_TEMPERATURE_CORRECTION_ENABLED,(double)v50);
  return failures==0U?0:1;
}
