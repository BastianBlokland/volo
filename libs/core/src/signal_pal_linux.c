#include "core_diag.h"
#include "core_thread.h"

#include <signal.h>

#include "signal_internal.h"

static i64 g_signal_states[Signal_Count];

static void signal_pal_interupt_handler(int signal) {
  thread_atomic_store_i64(&g_signal_states[Signal_Interupt], 1);
  (void)signal;
}

static void signal_pal_setup_interupt_handler() {
  struct sigaction action = (struct sigaction){
      .sa_handler = signal_pal_interupt_handler,
      .sa_flags   = SA_RESTART,
  };
  sigemptyset(&action.sa_mask);

  int res = sigaction(SIGINT, &action, null);
  diag_assert_msg(res == 0, "sigaction() failed: {}", fmt_int(res));
  (void)res;
}

void signal_pal_setup_handlers() { signal_pal_setup_interupt_handler(); }

bool signal_pal_is_received(Signal sig) { return thread_atomic_load_i64(&g_signal_states[sig]); }

void signal_pal_reset(Signal sig) { thread_atomic_store_i64(&g_signal_states[sig], 0); }
