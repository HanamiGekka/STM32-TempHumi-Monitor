/* Host-only paced TH3 stream; the model and serializer remain in production C. */
#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#undef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#include <windows.h>
#include <fcntl.h>
#include <io.h>
#else
#include <time.h>
#endif

#include "app_measurement_source.h"
#include "app_measurement_source_sim.h"
#include "app_protocol.h"

#define STREAM_SAMPLE_PERIOD_MS 100U

static uint64_t stream_monotonic_ms(void)
{
#ifdef _WIN32
  return (uint64_t)GetTickCount64();
#else
  struct timespec now;
  (void)clock_gettime(CLOCK_MONOTONIC, &now);
  return (uint64_t)now.tv_sec * 1000ULL + (uint64_t)now.tv_nsec / 1000000ULL;
#endif
}

static void stream_sleep_ms(uint32_t duration_ms)
{
#ifdef _WIN32
  Sleep((DWORD)duration_ms);
#else
  struct timespec delay = {(time_t)(duration_ms / 1000U),
                           (long)(duration_ms % 1000U) * 1000000L};
  (void)nanosleep(&delay, NULL);
#endif
}

int main(int argc,char **argv)
{
  uint64_t start_ms = stream_monotonic_ms();
  uint64_t next_sample_ms = start_ms;
  uint32_t seed = (uint32_t)(start_ms ^ (start_ms >> 32U));
  if(argc==2 &&strncmp(argv[1],"--seed=",7)==0) {
    char *end=NULL; unsigned long selected=strtoul(argv[1]+7,&end,0);
    if(end==argv[1]+7 ||*end!='\0') return 2;
    seed=(uint32_t)selected;
  } else if(argc!=1) return 2;
  measurement_data_t measurement;
  char frame[256];

#ifdef _WIN32
  /* The protocol already supplies CRLF; Windows text mode would add a second CR. */
  if (_setmode(_fileno(stdout), _O_BINARY) < 0)
  {
    return 1;
  }
#endif
  /* A host-time seed changes each run; all values still come from the C source. */
  app_measurement_source_sim_reset(0U, seed);
  for (;;)
  {
    uint64_t now_ms = stream_monotonic_ms();
    uint32_t timestamp_ms = (uint32_t)(now_ms - start_ms);

    app_measurement_source_read(&app_measurement_source_sim, &measurement, timestamp_ms);
    if (app_protocol_format_measurement_v3(frame, sizeof(frame), &measurement, APP_MEASUREMENT_SOURCE_SIMULATION, APP_MEASUREMENT_SOURCE_SIMULATION, 0U) < 0)
    {
      (void)fputs("TH3 formatting failed\n", stderr);
      return 1;
    }
    if ((fputs(frame, stdout) == EOF) || (fflush(stdout) == EOF))
    {
      return 0; /* The monitor closed its pipe. */
    }

    next_sample_ms += STREAM_SAMPLE_PERIOD_MS;
    now_ms = stream_monotonic_ms();
    if (next_sample_ms > now_ms)
    {
      stream_sleep_ms((uint32_t)(next_sample_ms - now_ms));
    }
    else if ((now_ms - next_sample_ms) > STREAM_SAMPLE_PERIOD_MS)
    {
      /* Drop missed publication slots; the source advances from elapsed time. */
      next_sample_ms = now_ms;
    }
  }
}
