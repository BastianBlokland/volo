#pragma once
#include "core_types.h"

/**
 * Time delta in nano-seconds, can be negative.
 */
typedef i64 TimeDuration;

/**
 * Nano-seconds since the start of the process steady clock.
 * Guaranteed to go forward (even if system clock changes).
 * Meant for precise time measurements, not for representing absolute moments in time.
 */
typedef i64 TimeSteady;

/**
 * Absolute moment in time.
 * Based on the system clock, can go backwards if the user changes the system clock.
 * Value is encoded in microseconds since epoch.
 */
typedef i64 TimeReal;

/**
 * TimeZone
 * Value is encoded in offset from UTC in minutes.
 */
typedef i16 TimeZone;

/**
 * Day of the week.
 */
typedef enum {
  TimeWeekDay_Monday = 0,
  TimeWeekDay_Tuesday,
  TimeWeekDay_Wednesday,
  TimeWeekDay_Thursday,
  TimeWeekDay_Friday,
  TimeWeekDay_Saturday,
  TimeWeekDay_Sunday,
} TimeWeekDay;

/**
 * Calendar Month.
 */
typedef enum {
  TimeMonth_January = 1,
  TimeMonth_February,
  TimeMonth_March,
  TimeMonth_April,
  TimeMonth_May,
  TimeMonth_June,
  TimeMonth_July,
  TimeMonth_August,
  TimeMonth_September,
  TimeMonth_October,
  TimeMonth_November,
  TimeMonth_December,
} TimeMonth;

/**
 * Calendar Date in the Gregorian calendar (without leap seconds).
 */
typedef struct {
  i32       year;
  TimeMonth month;
  u8        day;
} TimeDate;

/**
 * Duration constants.
 */

#define time_nanosecond ((TimeDuration)1)
#define time_microsecond (time_nanosecond * 1000)
#define time_millisecond (time_microsecond * 1000)
#define time_second (time_millisecond * 1000)
#define time_minute (time_second * 60)
#define time_hour (time_minute * 60)
#define time_day (time_hour * 24)

#define time_nanoseconds(_COUNT_) (time_nanosecond * (_COUNT_))
#define time_microseconds(_COUNT_) (time_microsecond * (_COUNT_))
#define time_milliseconds(_COUNT_) (time_millisecond * (_COUNT_))
#define time_seconds(_COUNT_) (time_second * (_COUNT_))
#define time_minutes(_COUNT_) (time_minute * (_COUNT_))
#define time_hours(_COUNT_) (time_hour * (_COUNT_))
#define time_days(_COUNT_) (time_day * (_COUNT_))

/**
 * Jan 1 1970 (Unix time)
 */
#define time_real_epoch ((TimeReal)0LL)

/**
 * Coordinated Universal Time (+00:00).
 */
#define time_zone_utc ((TimeZone)0)

/**
 * Observe the current steady clock.
 * The steady clock is guaranteed to only go forward and is meant for precise time measurements.
 */
TimeSteady time_steady_clock();

/**
 * Return the time duration between two steady measurements.
 */
TimeDuration time_steady_duration(TimeSteady from, TimeSteady to);

/**
 * Observe the system clock.
 */
TimeReal time_real_clock();

/**
 * Return the duration between two real times.
 */
TimeDuration time_real_duration(TimeReal from, TimeReal to);

/**
 * Offset a real-time by a duration.
 */
TimeReal time_real_offset(TimeReal, TimeDuration);

/**
 * Return which day of the week it is at the specified time.
 */
TimeWeekDay time_real_to_weekday(TimeReal);

/**
 * Return the Gregorian calender date at the specified time
 */
TimeDate time_real_to_date(TimeReal);

/**
 * Return the time at the specified Gregorian calender date.
 */
TimeReal time_date_to_real(TimeDate);

/**
 * Retrieve the current system timezone.
 */
TimeZone time_zone_current();

/**
 * Convert a timezone-offset to a duration.
 */
TimeDuration time_zone_to_duration(TimeZone);
