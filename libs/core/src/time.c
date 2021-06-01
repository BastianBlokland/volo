#include "core_diag.h"
#include "core_time.h"
#include "time_internal.h"

static bool g_intialized;

static i32 time_days_since_epoch(const TimeReal time) {
  return time / (time_day / time_microsecond);
}

void time_init() {
  time_pal_init();
  g_intialized = true;
}

TimeSteady time_steady_clock() {
  diag_assert_msg(g_intialized, "Time subsystem is not initialized, call core_init() at startup");
  return time_pal_steady_clock();
}

TimeDuration time_steady_duration(const TimeSteady from, const TimeSteady to) { return to - from; }

TimeReal time_real_clock() {
  diag_assert_msg(g_intialized, "Time subsystem is not initialized, call core_init() at startup");
  return time_pal_real_clock();
}

TimeReal time_real_offset(const TimeReal time, const TimeDuration duration) {
  return time + (duration / time_microsecond);
}

TimeWeekDay time_real_weekday(TimeReal time) {
  return (time_days_since_epoch(time) + TimeWeekDay_Thursday) % 7;
}

TimeZoneOffset time_zone_offset() {
  diag_assert_msg(g_intialized, "Time subsystem is not initialized, call core_init() at startup");
  return time_pal_zone_offset();
}
