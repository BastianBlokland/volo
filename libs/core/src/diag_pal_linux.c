#include "diag_internal.h"

#include <signal.h>
#include <unistd.h>

static bool g_debuggerPresent;

static void diag_sigtrap_handler(int signum) {
  (void)signum;

  g_debuggerPresent = false;
  signal(SIGTRAP, SIG_DFL);
}

void diag_pal_break() {
  g_debuggerPresent = true;
  signal(SIGTRAP, diag_sigtrap_handler);
  raise(SIGTRAP);
}

void diag_pal_crash() { _exit(1); }
