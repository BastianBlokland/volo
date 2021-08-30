#include "core_annotation.h"
#include "core_diag.h"

#include <Windows.h>

#include "time_internal.h"

static i64 g_perfCounterFrequency;

void time_pal_init() {
  LARGE_INTEGER freq;
  if (LIKELY(QueryPerformanceFrequency(&freq))) {
    g_perfCounterFrequency = freq.QuadPart;
  } else {
    g_perfCounterFrequency = 1;
  }
}

TimeSteady time_pal_steady_clock() {
  LARGE_INTEGER prefTicks;
  const BOOL    res = QueryPerformanceCounter(&prefTicks);
  if (UNLIKELY(!res)) {
    diag_crash_msg("QueryPerformanceCounter() failed");
  }
  return ((prefTicks.QuadPart * i64_lit(1000000) / g_perfCounterFrequency)) * i64_lit(1000);
}

TimeReal time_pal_real_clock() {
  FILETIME fileTime;
  GetSystemTimePreciseAsFileTime(&fileTime);
  return time_pal_native_to_real(&fileTime);
}

TimeZone time_pal_zone_current() {
  TIME_ZONE_INFORMATION timeZoneInfo;
  switch (GetTimeZoneInformation(&timeZoneInfo)) {
  case TIME_ZONE_ID_STANDARD:
    return -(TimeZone)(timeZoneInfo.Bias + timeZoneInfo.StandardBias);
  case TIME_ZONE_ID_DAYLIGHT:
    return -(TimeZone)(timeZoneInfo.Bias + timeZoneInfo.DaylightBias);
  case TIME_ZONE_ID_INVALID:
  default:
    diag_crash_msg("GetTimeZoneInformation() failed");
  }
}

TimeReal time_pal_native_to_real(const FILETIME* fileTime) {
  // Windows FILETIME is in 100 ns ticks since January 1 1601.
  const i64 winEpochToUnixEpoch = i64_lit(116444736000000000);
  const i64 winTickToMicro      = i64_lit(10);

  LARGE_INTEGER winTicks;
  winTicks.LowPart  = fileTime->dwLowDateTime;
  winTicks.HighPart = fileTime->dwHighDateTime;
  return (winTicks.QuadPart - winEpochToUnixEpoch) / winTickToMicro;
}
