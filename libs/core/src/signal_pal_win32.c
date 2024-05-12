#include "core_diag.h"
#include "core_thread.h"

#include "signal_internal.h"

#include <Windows.h>

static i64 g_signalCounters[Signal_Count];

static BOOL SYS_DECL signal_pal_report_handler(const DWORD dwCtrlType) {
  switch (dwCtrlType) {
  case CTRL_C_EVENT:
  case CTRL_BREAK_EVENT:
    /**
     * On Windows the distinction between Ctrl-C and Ctrl-Break is not as clear as Terminate vs
     * Interrupt on POSIX. For example we cannot send a Ctrl-C signal to a process group, which
     * makes it hard to use in practice. Therefore we treat both as having the same meaning.
     */
    thread_atomic_add_i64(&g_signalCounters[Signal_Terminate], 1);
    thread_atomic_add_i64(&g_signalCounters[Signal_Interrupt], 1);
    return true; // Indicate that we have handled the event.
  default:
    return false; // Indicate that we have not handled the event.
  }
}

static void signal_pal_setup_report_handler(void) {
  const BOOL success = SetConsoleCtrlHandler(signal_pal_report_handler, true);
  diag_assert_msg(success, "SetConsoleCtrlHandler() failed");
  (void)success;
}

void signal_pal_setup_handlers(void) { signal_pal_setup_report_handler(); }

i64 signal_pal_counter(const Signal sig) { return thread_atomic_load_i64(&g_signalCounters[sig]); }

void signal_pal_reset(const Signal sig) { thread_atomic_store_i64(&g_signalCounters[sig], 0); }
