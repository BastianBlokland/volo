#include "diag_internal.h"

#include <signal.h>
#include <sys/syscall.h>
#include <unistd.h>

#define diag_crash_exit_code 1

static bool g_debuggerPresent;

static void diag_sigtrap_handler(int signum) {
  (void)signum;

  g_debuggerPresent = false;
}

void diag_pal_break(void) {
  g_debuggerPresent = true;
  signal(SIGTRAP, diag_sigtrap_handler);
  raise(SIGTRAP);
}

void diag_pal_crash(void) {
  // NOTE: exit_group to terminate all threads in the process.
  syscall(SYS_exit_group, diag_crash_exit_code);
  UNREACHABLE
}
