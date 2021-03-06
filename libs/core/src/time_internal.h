#pragma once
#include "core_time.h"

void       time_pal_init();
TimeSteady time_pal_steady_clock();
TimeReal   time_pal_real_clock();
TimeZone   time_pal_zone_current();

#if defined(VOLO_LINUX)
struct timespec;
TimeReal time_pal_native_to_real(struct timespec);
#elif defined(VOLO_WIN32)
struct _FILETIME;
TimeReal time_pal_native_to_real(const struct _FILETIME*);
#endif
