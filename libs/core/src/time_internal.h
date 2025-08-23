#pragma once
#include "core/time.h"

void       time_pal_init(void);
TimeSteady time_pal_steady_clock(void);
TimeReal   time_pal_real_clock(void);
TimeZone   time_pal_zone_current(void);

#if defined(VOLO_LINUX)
struct timespec;
TimeReal time_pal_native_to_real(struct timespec);
#elif defined(VOLO_WIN32)
struct _FILETIME;
TimeReal time_pal_native_to_real(const struct _FILETIME*);
#endif
