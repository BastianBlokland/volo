#include "core_diag.h"
#include "core_time.h"
#include "time_internal.h"

static bool g_intialized;

void time_init() {
  time_pal_init();
  g_intialized = true;
}

TimeSteady time_clocksteady() {
  diag_assert_msg(g_intialized, "Time subsystem is not initialized, call core_init() at startup");
  return time_pal_clocksteady();
}
