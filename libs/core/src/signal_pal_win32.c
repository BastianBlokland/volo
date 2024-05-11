#include "core_diag.h"
#include "core_thread.h"

#include "signal_internal.h"

#include <Windows.h>

static u32 g_signal_states[Signal_Count];

static BOOL SYS_DECL signal_pal_interrupt_handler(const DWORD dwCtrlType) {
  switch (dwCtrlType) {
  case CTRL_C_EVENT:
  case CTRL_BREAK_EVENT:
    thread_atomic_store_u32(&g_signal_states[Signal_Interrupt], 1);
    return true; // Indicate that we have handled the event.
  default:
    return false; // Indicate that we have not handled the event.
  }
}

static void signal_pal_setup_interrupt_handler(void) {
  const BOOL success = SetConsoleCtrlHandler(signal_pal_interrupt_handler, true);
  diag_assert_msg(success, "SetConsoleCtrlHandler() failed");
  (void)success;
}

void signal_pal_setup_handlers(void) { signal_pal_setup_interrupt_handler(); }

bool signal_pal_is_received(const Signal sig) {
  return thread_atomic_load_u32(&g_signal_states[sig]);
}

void signal_pal_reset(const Signal sig) { thread_atomic_store_u32(&g_signal_states[sig], 0); }
