#pragma once
#include "core_types.h"

/**
 * Time delta in nano-seconds, can be negative.
 */
typedef i64 Duration;

#define time_nanosecond ((Duration)1)
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
 * Nano-seconds since the start of the process steady clock.
 * Guaranteed to go forward (even if system clock changes).
 * Meant for precise time measurements, not for representing absolute moments in time.
 */
typedef i64 TimeSteady;

/**
 * Observe the current steady clock.
 * The steady clock is guaranteed to only go forward and is meant for precise time measurements.
 */
TimeSteady time_clocksteady();
