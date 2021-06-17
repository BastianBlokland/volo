#include "core_diag.h"
#include "core_time.h"
#include "init_internal.h"
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

TimeDuration time_real_duration(const TimeReal from, const TimeReal to) {
  return time_microseconds(to - from);
}

TimeReal time_real_offset(const TimeReal time, const TimeDuration duration) {
  return time + (duration / time_microsecond);
}

TimeWeekDay time_real_to_weekday(const TimeReal time) {
  return (time_days_since_epoch(time) + TimeWeekDay_Thursday) % 7;
}

TimeDate time_real_to_date(const TimeReal time) {
  /**
   * Construct a Gregorian calendar date from micro-seconds since epoch.
   * Implementation based on: http://howardhinnant.github.io/date_algorithms.html#civil_from_days
   */
  const i64 z           = time_days_since_epoch(time) + 719468;
  const i32 era         = (z >= 0 ? z : z - 146096) / 146097;
  const i32 dayOfEra    = z - era * 146097;
  const i32 yearOfEra   = (dayOfEra - dayOfEra / 1460 + dayOfEra / 36524 - dayOfEra / 146096) / 365;
  const i32 year        = yearOfEra + era * 400;
  const i32 dayOfYear   = dayOfEra - (365 * yearOfEra + yearOfEra / 4 - yearOfEra / 100);
  const i32 mp          = (5 * dayOfYear + 2) / 153;
  const i32 day         = dayOfYear - (153 * mp + 2) / 5 + 1;
  const TimeMonth month = mp + (mp < 10 ? 3 : -9);
  return (TimeDate){
      .year  = year + (month <= 2),
      .month = month,
      .day   = day,
  };
}

TimeReal time_date_to_real(const TimeDate date) {
  /**
   * Convert a Gregorian calendar date to micro-seconds since epoch.
   * Implementation based on: http://howardhinnant.github.io/date_algorithms.html#days_from_civil
   */
  const i32 year      = date.year - (date.month <= 2);
  const i32 era       = (year >= 0 ? year : year - 399) / 400;
  const i32 yearOfEra = year - era * 400;
  const i32 dayOfYear = (153 * (date.month + (date.month > 2 ? -3 : 9)) + 2) / 5 + (date.day - 1);
  const i32 dayOfEra  = yearOfEra * 365 + yearOfEra / 4 - yearOfEra / 100 + dayOfYear;
  const i32 daysSinceEpoch = era * 146097 + dayOfEra - 719468;
  return (i64)daysSinceEpoch * (time_day / time_microsecond);
}

TimeZone time_zone_current() {
  diag_assert_msg(g_intialized, "Time subsystem is not initialized, call core_init() at startup");
  return time_pal_zone_current();
}

TimeDuration time_zone_to_duration(const TimeZone timezone) { return time_minutes(timezone); }
