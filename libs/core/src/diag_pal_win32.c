#include "diag_internal.h"

#include <Windows.h>

#define diag_crash_exit_code 1

static THREAD_LOCAL jmp_buf* g_exceptAnchor;

void diag_pal_except_enable(jmp_buf* anchor, const i32 exceptionCode) {
  static i32 g_exceptHandlerInstalled;

  if (exceptionCode) {
    /**
     * An exception has occurred, report the crash with the recorded stack.
     */
    diag_assert(!g_exceptAnchor); // Anchors should be removed when an exception occurs.

    // TODO: Report the crash.
    diag_pal_crash();
  } else {
    /**
     * Enable exception interception with the new anchor.
     */
    diag_assert_msg(!g_exceptAnchor, "Exception interception was already active for this thread");
    g_exceptAnchor = anchor;

    // TODO: Setup exception handler.
  }
}

void diag_pal_except_disable(void) {
  diag_assert_msg(g_exceptAnchor, "Exception interception was not active for this thread");
  g_exceptAnchor = null;
}

void diag_pal_break(void) {
  if (IsDebuggerPresent()) {
    DebugBreak();
  }
}

void diag_pal_crash(void) {
  HANDLE curProcess = GetCurrentProcess();
  TerminateProcess(curProcess, diag_crash_exit_code);
  UNREACHABLE
}
