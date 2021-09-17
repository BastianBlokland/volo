#include "core_diag.h"
#include "core_thread.h"

#include "signal_internal.h"

#include <Windows.h>

static i64 g_signal_states[Signal_Count];

static BOOL signal_pal_interupt_handler(DWORD dwCtrlType) {
  switch (dwCtrlType) {
  case CTRL_C_EVENT:
  case CTRL_BREAK_EVENT:
    thread_atomic_store_i64(&g_signal_states[Signal_Interupt], 1);
    return true; // Indicate that we have handled the event.
  default:
    return false; // Indicate that we have not handled the event.
  }
}

static void signal_pal_setup_interupt_handler() {
  BOOL success = SetConsoleCtrlHandler(signal_pal_interupt_handler, true);
  diag_assert_msg(success, "SetConsoleCtrlHandler() failed");
  (void)success;
}

void signal_pal_setup_handlers() { signal_pal_setup_interupt_handler(); }

bool signal_pal_is_received(Signal sig) { return thread_atomic_load_i64(&g_signal_states[sig]); }

void signal_pal_reset(Signal sig) { thread_atomic_store_i64(&g_signal_states[sig], 0); }
