#pragma once
#include "core_types.h"

/**
 * Time delta in nano-seconds, can be negative.
 */
typedef i64 Duration;

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
