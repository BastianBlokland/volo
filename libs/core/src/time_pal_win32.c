#include "core_annotation.h"
#include "core_diag.h"
#include "time_internal.h"
#include <Windows.h>

static i64 g_perfCounterFrequency;

void time_pal_init() {
  LARGE_INTEGER freq;
  if (likely(QueryPerformanceFrequency(&freq))) {
    g_perfCounterFrequency = freq.QuadPart;
  } else {
    g_perfCounterFrequency = 1;
  }
}

TimeSteady time_pal_clocksteady() {
  LARGE_INTEGER prefTicks;
  const BOOL    res = QueryPerformanceCounter(&prefTicks);
  diag_assert_msg(res, "QueryPerformanceCounter() failed");
  (void)res;
  return prefTicks.QuadPart * 1000000LL / g_perfCounterFrequency * 1000LL;
}
