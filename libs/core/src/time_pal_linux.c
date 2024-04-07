#include "core_diag.h"

#include "time_internal.h"

#include <time.h>

void time_pal_init(void) {}

TimeSteady time_pal_steady_clock(void) {
  struct timespec ts;
  const int       res = clock_gettime(CLOCK_MONOTONIC, &ts);
  if (UNLIKELY(res != 0)) {
    diag_crash_msg("clock_gettime(CLOCK_MONOTONIC) failed: {}", fmt_int(res));
  }
  return ts.tv_sec * i64_lit(1000000000) + ts.tv_nsec;
}

TimeReal time_pal_real_clock(void) {
  struct timespec ts;
  const int       res = clock_gettime(CLOCK_REALTIME, &ts);
  if (UNLIKELY(res != 0)) {
    diag_crash_msg("clock_gettime(CLOCK_REALTIME) failed: {}", fmt_int(res));
  }
  return time_pal_native_to_real(ts);
}

TimeZone time_pal_zone_current(void) {
  const time_t utcSeconds            = time(null);
  const time_t localSeconds          = timegm(localtime(&utcSeconds));
  const time_t timezoneOffsetSeconds = localSeconds - utcSeconds;
  const time_t timezoneOffsetMinutes = timezoneOffsetSeconds / 60;
  return (TimeZone)timezoneOffsetMinutes;
}

TimeReal time_pal_native_to_real(struct timespec ts) {
  return ts.tv_sec * i64_lit(1000000) + ts.tv_nsec / i64_lit(1000);
}
