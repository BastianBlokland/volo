#pragma once
#include "core_time.h"

void           time_pal_init();
TimeSteady     time_pal_steady_clock();
TimeReal       time_pal_real_clock();
TimeZoneOffset time_pal_zone_offset();
