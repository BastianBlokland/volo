#include "core_diag.h"
#include "core_thread.h"

#include "signal_internal.h"

#include <signal.h>

static i32 g_signalStates[Signal_Count];

static void SYS_DECL signal_pal_interrupt_handler(const int signal) {
  thread_atomic_store_i32(&g_signalStates[Signal_Interrupt], 1);
  (void)signal;
}

static void signal_pal_setup_interrupt_handler() {
  struct sigaction action = {
      .sa_handler = signal_pal_interrupt_handler,
      .sa_flags   = SA_RESTART,
  };
  sigemptyset(&action.sa_mask);

  const int res = sigaction(SIGINT, &action, null);
  if (UNLIKELY(res != 0)) {
    diag_crash_msg("sigaction() failed: {}", fmt_int(res));
  }
}

void signal_pal_setup_handlers() { signal_pal_setup_interrupt_handler(); }

bool signal_pal_is_received(const Signal sig) {
  return thread_atomic_load_i32(&g_signalStates[sig]);
}

void signal_pal_reset(const Signal sig) { thread_atomic_store_i32(&g_signalStates[sig], 0); }
