#include "core_diag.h"
#include "core_thread.h"

#include "signal_internal.h"

#include <signal.h>

typedef struct {
  int posixSignal;
  i64 counter;
} SignalReport;

/**
 * Configuration of report signals.
 * For report signals we track a resettable counter of how many times they have been triggered.
 */
static SignalReport g_signalReport[Signal_Count] = {
    [Signal_Terminate] = {.posixSignal = SIGTERM},
    [Signal_Interrupt] = {.posixSignal = SIGINT},
};

static void SYS_DECL signal_pal_report_handler(const int posixSignal) {
  for (Signal s = 0; s != Signal_Count; ++s) {
    if (g_signalReport[s].posixSignal == posixSignal) {
      thread_atomic_add_i64(&g_signalReport[s].counter, 1);
      break;
    }
  }
}

static void signal_pal_setup_report_handler(void) {
  struct sigaction action = {
      .sa_handler = signal_pal_report_handler,
      .sa_flags   = SA_RESTART,
  };
  sigemptyset(&action.sa_mask);

  for (Signal s = 0; s != Signal_Count; ++s) {
    if (g_signalReport[s].posixSignal) {
      const int res = sigaction(g_signalReport[s].posixSignal, &action, null);
      if (UNLIKELY(res != 0)) {
        diag_crash_msg("sigaction() failed: {}", fmt_int(res));
      }
    }
  }
}

void signal_pal_setup_handlers(void) { signal_pal_setup_report_handler(); }

i64 signal_pal_counter(const Signal sig) {
  return thread_atomic_load_i64(&g_signalReport[sig].counter);
}

void signal_pal_reset(const Signal sig) {
  thread_atomic_store_i64(&g_signalReport[sig].counter, 0);
}
