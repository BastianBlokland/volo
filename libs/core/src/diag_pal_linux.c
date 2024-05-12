#include "core_thread.h"

#include "diag_internal.h"

#include <signal.h>
#include <sys/syscall.h>
#include <unistd.h>

#define diag_crash_exit_code 1

static void diag_trap_handler(MAYBE_UNUSED const int posixSignal) {
  /**
   * Normally do nothing when a break-point is hit, the debugger will catch it if its present.
   */
}

void diag_pal_break(void) {
  static i32 g_trapHandlerInstalled;
  if (!thread_atomic_exchange_i32(&g_trapHandlerInstalled, true)) {
    struct sigaction action = {.sa_handler = diag_trap_handler};
    sigemptyset(&action.sa_mask);
    sigaction(SIGTRAP, &action, null);
  }
  raise(SIGTRAP);
}

void diag_pal_crash(void) {
  // NOTE: exit_group to terminate all threads in the process.
  syscall(SYS_exit_group, diag_crash_exit_code);
  UNREACHABLE
}
