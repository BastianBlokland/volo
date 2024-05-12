#include "core_array.h"
#include "core_symbol.h"
#include "core_thread.h"

#include "diag_internal.h"

#include <signal.h>
#include <sys/syscall.h>
#include <unistd.h>

#define diag_crash_exit_code 1

typedef struct {
  int    posixSignal;
  String name;
} DiagException;

static const DiagException g_exceptConfig[] = {
    {.posixSignal = SIGABRT, .name = string_static("abort")},
    {.posixSignal = SIGBUS, .name = string_static("bus-error")},
    {.posixSignal = SIGFPE, .name = string_static("floating-point-exception")},
    {.posixSignal = SIGILL, .name = string_static("illegal-instruction")},
    {.posixSignal = SIGQUIT, .name = string_static("quit")},
    {.posixSignal = SIGSEGV, .name = string_static("segmentation-fault")},
};
static THREAD_LOCAL jmp_buf*    g_exceptAnchor;
static THREAD_LOCAL SymbolStack g_exceptStack;

static String diag_exception_name(const int posixSignal) {
  array_for_t(g_exceptConfig, DiagException, except) {
    if (except->posixSignal == posixSignal) {
      return except->name;
    }
  }
  diag_pal_crash();
}

static void SYS_DECL diag_exception_handler(const int posixSignal) {
  jmp_buf* anchor = g_exceptAnchor;
  g_exceptAnchor  = null; // Clear anchor to avoid triggering it multiple times.

  if (anchor) {
    /**
     * An exception occurred and we have an handler. To report the crash we collect a stack-trace
     * while the offending call-chain is still on the stack and then jump to the anchor for
     * reporting the crash.
     * Reason for not reporting the crash here is that the crash reporting is not signal safe.
     *
     * TODO: For SIGILL, SIGFPE, SIGSEGV, SIGBUS we get the address of the fault in 'si_addr' we
     * should include this in the crash report.
     */
    g_exceptStack = symbol_stack_walk();
    longjmp(*anchor, posixSignal); // Jump to the anchor, will call 'diag_except_enable()' again.
  } else {
    /**
     * No anchor was configured for this thread so we cannot report the crash. In this case we
     * restore the default signal handler and invoke it.
     * NOTE: Because exceptions are always fatal we don't need to restore our handler.
     */
    signal(posixSignal, SIG_DFL);
    raise(posixSignal);
  }
}

void diag_pal_except_enable(jmp_buf* anchor, const i32 exceptionCode) {
  static i32 g_exceptHandlerInstalled;

  if (exceptionCode) {
    /**
     * An exception has occurred, report the crash with the recorded stack.
     */
    diag_assert(!g_exceptAnchor); // Anchors should be removed when an exception occurs.

    DynString msg = dynstring_create_over(mem_stack(128));
    fmt_write(&msg, "Exception: {}\n", fmt_text(diag_exception_name(exceptionCode)));
    diag_crash_report(&g_exceptStack, dynstring_view(&msg));
    diag_pal_crash();
  } else {
    /**
     * Enable exception interception with the new anchor.
     */
    diag_assert_msg(!g_exceptAnchor, "Exception interception was already active for this thread");
    g_exceptAnchor = anchor;

    if (!thread_atomic_exchange_i32(&g_exceptHandlerInstalled, true)) {
      struct sigaction action = {.sa_handler = diag_exception_handler};
      sigemptyset(&action.sa_mask);
      array_for_t(g_exceptConfig, DiagException, except) {
        sigaction(except->posixSignal, &action, null);
      }
    }
  }
}

void diag_pal_except_disable(void) {
  diag_assert_msg(g_exceptAnchor, "Exception interception was not active for this thread");
  g_exceptAnchor = null;
}

static void SYS_DECL diag_trap_handler(MAYBE_UNUSED const int posixSignal) {
  /**
   * Do nothing when a break-point is hit, the debugger will catch it if its present.
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
