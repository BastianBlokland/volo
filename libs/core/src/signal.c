#include "core_diag.h"
#include "core_thread.h"

#include "init_internal.h"
#include "signal_internal.h"

static i32 g_signalIntercept;

void signal_intercept_enable(void) {
  if (!thread_atomic_exchange_i32(&g_signalIntercept, true)) {
    signal_pal_setup_handlers();
  }
}

bool signal_is_received(const Signal sig) {
  diag_assert_msg(thread_atomic_load_i32(&g_signalIntercept), "Signal interception is not active");
  return signal_pal_counter(sig) != 0;
}

void signal_reset(const Signal sig) {
  diag_assert_msg(thread_atomic_load_i32(&g_signalIntercept), "Signal interception is not active");
  signal_pal_reset(sig);
}
