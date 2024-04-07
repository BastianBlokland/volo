#include "core_diag.h"
#include "core_thread.h"

#include "init_internal.h"
#include "signal_internal.h"

static i32 g_signalInterceptActive;

void signal_intercept_enable(void) {
  i32 expectedActive = false;
  if (thread_atomic_compare_exchange_i32(&g_signalInterceptActive, &expectedActive, true)) {
    signal_pal_setup_handlers();
  }
}

bool signal_is_received(const Signal sig) {
  diag_assert_msg(g_signalInterceptActive, "Signal interception is not active");
  return signal_pal_is_received(sig);
}

void signal_reset(const Signal sig) {
  diag_assert_msg(g_signalInterceptActive, "Signal interception is not active");
  signal_pal_reset(sig);
}
