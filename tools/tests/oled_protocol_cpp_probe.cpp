#include "app_protocol.h"
#include "algo_sensor_convert.h"
#include <cstdio>
#include <cstring>
#include <cmath>
#define CHECK(condition) do { if (!(condition)) { std::fprintf(stderr,"FAIL: %s\n",#condition); return 1; } } while (false)

int main()
{
    char text[224];
    const int length=app_protocol_format_oled_temperature(text,sizeof(text),1234,
        27.025f,APP_MEASUREMENT_SOURCE_REAL,1,1,"T:27.0C",1,0);
    CHECK(length>0 && std::strstr(text,"source=REAL profile=1 cal=1"));
    CHECK(std::strstr(text,"temperature=27.025C text=T:27.0C shown=1 status=0\r\n"));
    CHECK(app_protocol_format_oled_temperature(text,sizeof(text),1235,NAN,
        APP_MEASUREMENT_SOURCE_SIMULATION,1,0,"T:NAC",0,1)>0);
    CHECK(std::strstr(text,"temperature=NAC") && std::strstr(text,"status=1"));
    CHECK(app_protocol_format_oled_temperature(nullptr,0,0,0,
        APP_MEASUREMENT_SOURCE_REAL,0,0,nullptr,0,0)<0);
    uint16_t calibration=0;
    CHECK(algo_temperature_correct_profile(26.125f,2,&calibration)>26.125f && calibration==2);
    return 0;
}
