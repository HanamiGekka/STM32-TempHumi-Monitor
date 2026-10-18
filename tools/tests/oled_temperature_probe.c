/* Integrate the actual application, algorithm, serializer and unmodified OLED
 * driver through a modeled I2C controller. This is NOT an optical hardware test. */
#include "oled_host_hal.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include "../../Core/Src/app_th_meter.c"
#include "../../Core/Src/drv_oled.c"

I2C_HandleTypeDef hi2c1, hi2c2;
UART_HandleTypeDef huart1;
static uint32_t host_tick;
static unsigned checks, windows, i2c_calls, fail_at;
static uint8_t panel[8][128], panel_page, panel_column;
static char wire[16384];
static const char alphabet[]=" 0123456789.%:/_-ABCDEHIKLMNOPRSTUVWv";

static void check(int pass, const char *name)
{
  checks++;
  if (!pass) { fprintf(stderr,"FAIL %s at window %u\n",name,windows); exit(2); }
}
uint32_t HAL_GetTick(void) { return host_tick; }
void HAL_Delay(uint32_t delay) { host_tick += delay; }
HAL_StatusTypeDef HAL_I2C_IsDeviceReady(I2C_HandleTypeDef *h, uint16_t a, uint32_t t, uint32_t m)
{ (void)h; (void)a; (void)t; (void)m; return HAL_OK; }
HAL_StatusTypeDef HAL_I2C_Master_Transmit(I2C_HandleTypeDef *h, uint16_t a, uint8_t *data, uint16_t n, uint32_t timeout)
{
  (void)h; (void)a; (void)timeout;
  if (++i2c_calls==fail_at) { return HAL_TIMEOUT; }
  if (n<2) { return HAL_ERROR; }
  if (data[0]==0) {
    uint8_t c=data[1];
    if (c>=0xB0 && c<=0xB7) { panel_page=c-0xB0; }
    else if (c<=0x0F) { panel_column=(uint8_t)((panel_column&0xF0)|c); }
    else if (c>=0x10 && c<=0x1F) { panel_column=(uint8_t)((panel_column&0x0F)|((c&0x0F)<<4)); }
  } else if(data[0]==0x40) {
    for(uint16_t i=1;i<n;i++) {
      if(panel_column<128) { panel[panel_page][panel_column]=data[i]; }
      panel_column++;
    }
  }
  return HAL_OK;
}
HAL_StatusTypeDef bsp_uart_log_init(UART_HandleTypeDef *p) { (void)p; return HAL_OK; }
HAL_StatusTypeDef bsp_uart_log_write(const char *p)
{ size_t used=strlen(wire),size=strlen(p); if(used+size>=sizeof(wire)) { exit(3); } memcpy(wire+used,p,size+1); return HAL_OK; }
uint8_t bsp_i2c_scan_bus(I2C_HandleTypeDef *p,const char *n) { (void)p;(void)n;return 1; }
void bsp_led_key_led_on(void) {}
void bsp_led_key_led_off(void) {}
void bsp_led_key_led_toggle(void) {}
GPIO_PinState bsp_led_key_read(void) { return GPIO_PIN_SET; }
bool bsp_led_key_is_pressed(void) { return false; }
HAL_StatusTypeDef drv_ads1115_init(void) { return HAL_OK; }
HAL_StatusTypeDef drv_ads1115_probe(void) { return HAL_OK; }
HAL_StatusTypeDef drv_ads1115_read_raw(uint8_t ch,int16_t *p) { *p=ch==0?11200:9600;return HAL_OK; }
HAL_StatusTypeDef drv_ads1115_get_last_diag(drv_ads1115_diag_reason_t *p) { *p=DRV_ADS1115_DIAG_NONE;return HAL_OK; }
float drv_ads1115_raw_to_voltage(int16_t v) { return (float)v*.000125f; }
HAL_StatusTypeDef drv_humidity_pwm_init(void) { return HAL_OK; }
HAL_StatusTypeDef drv_humidity_pwm_start(void) { return HAL_OK; }
HAL_StatusTypeDef drv_humidity_pwm_get_duty(float *p) { *p=NAN;return HAL_BUSY; }
HAL_StatusTypeDef drv_humidity_pwm_get_status(drv_humidity_pwm_status_t *p) { memset(p,0,sizeof(*p));return HAL_OK; }

static void read_row(unsigned row,char *out)
{
  unsigned i;
  for(i=0;i<21;i++) {
    out[i]='?';
    for(const char *p=alphabet;*p;p++) {
      if(!memcmp(&panel[row][i*6],drv_oled_get_glyph(*p),5)) { out[i]=*p; break; }
    }
  }
  out[i]=0;
  while(i && out[i-1]==' ') { out[--i]=0; }
}
static void verify_render(unsigned profile, int tsrc, int hsrc,float raw_temp,int valid_t,int valid_h,unsigned page)
{
  app_th_meter_sample_t sample;
  char temperature[24], humidity[24], row[24], expected_diag[64];
  memset(&sample,0,sizeof(sample));
  sample.temperature_c=raw_temp; sample.humidity_rh=55.5f;
  sample.v_ntc=1.4f; sample.v_chs1101=1.2f; sample.rh_adc=55.5f;sample.rh_pwm=NAN;
  sample.valid_flags=(valid_t?APP_MEASUREMENT_VALID_TEMPERATURE:0U)|(valid_h?APP_MEASUREMENT_VALID_HUMIDITY:0U);
  sample.source_type=tsrc==hsrc?(app_measurement_source_type_t)tsrc:APP_MEASUREMENT_SOURCE_MIXED;
  s_hybrid_mode.effective_temperature=(app_measurement_source_type_t)tsrc;
  s_hybrid_mode.effective_humidity=(app_measurement_source_type_t)hsrc;
  s_hybrid_mode.pending=false;
  s_calibration_effective=(uint16_t)profile; s_calibration_requested=(uint16_t)profile;
  s_app_th_meter.oled_page_index=(uint8_t)page;
  app_th_meter_sample_accumulator_reset(&s_app_th_meter.sample_accumulator);
  for(unsigned i=0;i<16;i++) { app_th_meter_sample_accumulator_add(&s_app_th_meter.sample_accumulator,&sample); }
  app_th_meter_sample_accumulator_finalize(&s_app_th_meter.sample_accumulator,&sample);
  float expected=sample.temperature_c;
  uint16_t cal=0;
  if(tsrc==APP_MEASUREMENT_SOURCE_REAL && valid_t) { expected=algo_temperature_correct_profile(expected,(uint16_t)profile,&cal); }
  app_th_meter_format_oled_value(temperature,sizeof(temperature),"T:",valid_t?expected:NAN,"C");
  app_th_meter_format_oled_value(humidity,sizeof(humidity),"RH:",valid_h?55.5f:NAN,"%");
  host_tick+=1020; s_app_th_meter.last_sample_publish_tick=host_tick-1001;
  wire[0]=0; i2c_calls=0; fail_at=0;
  app_th_meter_try_publish_sample_window(); windows++;
  check(strstr(wire,"TH3,")!=NULL,"TH3 preserved");
  check(strstr(wire,"[OLED_TEMP]")!=NULL,"OLED evidence emitted");
  check(strstr(wire,"status=0\r\n")!=NULL,"complete HAL success reported");
  snprintf(expected_diag,sizeof(expected_diag),"text=%s shown=%u",temperature,(tsrc==hsrc && tsrc==0 && page<2)?0U:1U);
  check(strstr(wire,expected_diag)!=NULL,"OLED evidence matches current window and page");
  if(!valid_t || !isfinite(expected)) {
    check(strstr(wire,"temperature=NAC text=T:NAC")!=NULL,"invalid OLED evidence is NA, not a numeric sentinel");
  }
  if(tsrc==hsrc && tsrc==0 && page<2) { return; }
  read_row(2,row); check(!strcmp(row,temperature),"actual glyph bytes encode corrected/valid temperature");
  read_row(0,row);check(strchr(row,'?')==NULL,"caption uses existing font glyphs");
  read_row(4,row);check(strstr(row,humidity)!=NULL,"humidity validity remains explicit");
  read_row(tsrc==hsrc && tsrc==0?0:6,row);
  if(tsrc==1) { check(strstr(row,"CAL:NONE SIM")!=NULL,"SIM never claims correction"); }
  else if(!valid_t || !isfinite(expected)) { check(strstr(row," NA")!=NULL,"invalid temperature correction unavailable"); }
  else if(profile==0) { check(strstr(row,"CAL:NONE")!=NULL,"Off visible"); }
  else { check(strstr(row,cal?"PROV":"BASE")!=NULL,"provisional versus inactive baseline distinct"); }
}
int main(void)
{
  check(drv_oled_init()==HAL_OK,"actual driver initialization through modeled bus");
  const float inputs[]={-10.0f,25.0f,25.2f,25.3f,26.125f,30.4f,35.5f,35.6f,40.0f,50.0f,NAN,INFINITY};
  for(unsigned p=0;p<4;p++) for(int t=0;t<2;t++) for(int h=0;h<2;h++)
    for(unsigned k=0;k<sizeof(inputs)/sizeof(inputs[0]);k++)
      for(unsigned page=0;page<(t==0&&h==0?3U:1U);page++)
        verify_render(p,t,h,inputs[k],1,1,page);
  for(int t=0;t<2;t++) for(int h=0;h<2;h++)
    for(unsigned p=0;p<4;p++) for(int valid_t=0;valid_t<2;valid_t++)
      for(int valid_h=0;valid_h<2;valid_h++)
        if(!valid_t || !valid_h) verify_render(p,t,h,26.125f,valid_t,valid_h,2);

  /* Requests cannot make the OLED jump ahead of the current serialized window. */
  s_hybrid_mode.effective_temperature=APP_MEASUREMENT_SOURCE_REAL;
  s_hybrid_mode.effective_humidity=APP_MEASUREMENT_SOURCE_SIMULATION;
  app_th_meter_sample_t sample={0}; sample.temperature_c=26.125f;sample.humidity_rh=55;
  sample.valid_flags=APP_MEASUREMENT_VALID_TEMPERATURE|APP_MEASUREMENT_VALID_HUMIDITY;
  s_calibration_effective=0;s_calibration_requested=3;
  app_th_meter_sample_accumulator_reset(&s_app_th_meter.sample_accumulator);
  for(unsigned i=0;i<16;i++)app_th_meter_sample_accumulator_add(&s_app_th_meter.sample_accumulator,&sample);
  host_tick+=1020;wire[0]=0;app_th_meter_try_publish_sample_window();
  check(strstr(wire,"profile=0 cal=0")!=NULL,"pending choice does not prelabel old output");
  check(s_calibration_effective==3,"choice becomes effective at next window");

  char temp[24], row[24];uint8_t shown;HAL_StatusTypeDef status;
  sample.source_type=APP_MEASUREMENT_SOURCE_MIXED;
  app_th_meter_format_oled_value(temp,sizeof(temp),"T:",sample.temperature_c,"C");
  i2c_calls=0;fail_at=0;shown=0;
  check(app_th_meter_show_oled_page(&sample,temp,&shown)==HAL_OK && shown==1,
        "baseline render before exhaustive per-write failure injection");
  unsigned injected=i2c_calls;
  for(unsigned n=1;n<=injected;n++) {
    i2c_calls=0;fail_at=n;shown=99;
    status=app_th_meter_show_oled_page(&sample,temp,&shown);
    check(status!=HAL_OK && shown==0,"I2C failure never reported as successful display");
  }
  wire[0]=0;i2c_calls=0;fail_at=1;
  app_th_meter_publish_sample(&sample);
  check(strstr(wire,"shown=0 status=1\r\n")!=NULL,
        "actual publication propagates display transport failure in diagnostic");
  fail_at=0;
  char protocol[224];
  check(app_protocol_format_oled_temperature(NULL,sizeof(protocol),0,26.0f,0,1,1,temp,1,0)<0,"null evidence buffer rejected");
  check(app_protocol_format_oled_temperature(protocol,0,0,26.0f,0,1,1,temp,1,0)<0,"zero evidence buffer rejected");
  check(app_protocol_format_oled_temperature(protocol,sizeof(protocol),0,26.0f,0,1,1,NULL,1,0)<0,"null glyph text rejected");
  check(app_protocol_format_oled_temperature(protocol,4,0,26.0f,0,1,1,temp,1,0)<0,"truncated evidence rejected");
  check(app_th_meter_show_oled_page(NULL,temp,&shown)!=HAL_OK,"null sample rejected");
  check(app_th_meter_show_oled_page(&sample,NULL,&shown)!=HAL_OK,"null text rejected");
  check(app_th_meter_show_oled_page(&sample,temp,NULL)!=HAL_OK,"null shown flag rejected");
  for(unsigned p=0;p<4;p++) {
    verify_render(p,0,1,26.125f,1,1,2);
    read_row(2,row);printf("%s{\"profile\":%u,\"temperature_text\":\"%s\",",p?",":"{\"examples\":[",p,row);
    read_row(6,row);printf("\"calibration_text\":\"%s\"}",row);
  }
  printf("],\"pass\":true,\"checks\":%u,\"windows\":%u,\"injected_I2C_failures\":%u,\"classification\":\"HOST I2C/controller model, not physical pixels\"}\n",checks,windows,injected);
  return 0;
}
