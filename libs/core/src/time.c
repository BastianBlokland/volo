#include "core_diag.h"
#include "core_time.h"
#include "time_internal.h"

static bool g_intialized;

void time_init() {
  time_pal_init();
  g_intialized = true;
}

TimeDuration time_steady_duration(const TimeSteady from, const TimeSteady to) { return to - from; }

TimeSteady time_steady_clock() {
  diag_assert_msg(g_intialized, "Time subsystem is not initialized, call core_init() at startup");
  return time_pal_steady_clock();
}

TimeReal time_real_clock() {
  diag_assert_msg(g_intialized, "Time subsystem is not initialized, call core_init() at startup");
  return time_pal_real_clock();
}

TimeZoneOffset time_zone_offset() {
  diag_assert_msg(g_intialized, "Time subsystem is not initialized, call core_init() at startup");
  return time_pal_zone_offset();
}
