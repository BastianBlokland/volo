#include "core_diag.h"
#include "time_internal.h"
#include <time.h>

void time_pal_init() {}

TimeSteady time_pal_clocksteady() {
  struct timespec ts;
  const int       res = clock_gettime(CLOCK_MONOTONIC, &ts);
  diag_assert_msg(res == 0, "clock_gettime(CLOCK_MONOTONIC) failed");
  (void)res;
  return ts.tv_sec * 1000000000LL + ts.tv_nsec;
}
