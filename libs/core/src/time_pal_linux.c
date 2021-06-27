#include "core_diag.h"

#include <time.h>

#include "time_internal.h"

void time_pal_init() {}

TimeSteady time_pal_steady_clock() {
  struct timespec ts;
  const int       res = clock_gettime(CLOCK_MONOTONIC, &ts);
  diag_assert_msg(res == 0, "clock_gettime(CLOCK_MONOTONIC) failed: {}", fmt_int(res));
  (void)res;
  return ts.tv_sec * i64_lit(1000000000) + ts.tv_nsec;
}

TimeReal time_pal_real_clock() {
  struct timespec ts;
  const int       res = clock_gettime(CLOCK_REALTIME, &ts);
  diag_assert_msg(res == 0, "clock_gettime(CLOCK_REALTIME) failed: {}", fmt_int(res));
  (void)res;
  return ts.tv_sec * i64_lit(1000000) + ts.tv_nsec / i64_lit(1000);
}

TimeZone time_pal_zone_current() {
  const time_t utcSeconds            = time(null);
  const time_t localSeconds          = timegm(localtime(&utcSeconds));
  const time_t timezoneOffsetSeconds = localSeconds - utcSeconds;
  const time_t timezoneOffsetMinutes = timezoneOffsetSeconds / 60;
  return (TimeZone)timezoneOffsetMinutes;
}
