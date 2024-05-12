#include "core_symbol.h"
#include "core_thread.h"

#include "diag_internal.h"

#include <Windows.h>

#define diag_crash_exit_code 1

static THREAD_LOCAL jmp_buf*    g_exceptAnchor;
static THREAD_LOCAL SymbolStack g_exceptStack;
static THREAD_LOCAL uptr        g_exceptAddr;

static bool diag_except_handle(const i32 exceptCode) {
  switch (exceptCode) {
  case EXCEPTION_ACCESS_VIOLATION:
  case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:
  case EXCEPTION_DATATYPE_MISALIGNMENT:
  case EXCEPTION_FLT_DENORMAL_OPERAND:
  case EXCEPTION_FLT_DIVIDE_BY_ZERO:
  case EXCEPTION_FLT_INEXACT_RESULT:
  case EXCEPTION_FLT_INVALID_OPERATION:
  case EXCEPTION_FLT_OVERFLOW:
  case EXCEPTION_FLT_STACK_CHECK:
  case EXCEPTION_FLT_UNDERFLOW:
  case EXCEPTION_ILLEGAL_INSTRUCTION:
  case EXCEPTION_IN_PAGE_ERROR:
  case EXCEPTION_INT_DIVIDE_BY_ZERO:
  case EXCEPTION_INT_OVERFLOW:
  case EXCEPTION_STACK_OVERFLOW:
    return true;
  default:
    return false;
  }
}

static String diag_except_name(const i32 exceptCode) {
  // clang-format off
  switch (exceptCode) {
  case EXCEPTION_ACCESS_VIOLATION:      return string_lit("access-violation");
  case EXCEPTION_ARRAY_BOUNDS_EXCEEDED: return string_lit("array-bounds-exceeded");
  case EXCEPTION_DATATYPE_MISALIGNMENT: return string_lit("datatype-misalignment");
  case EXCEPTION_FLT_DENORMAL_OPERAND:  return string_lit("float-denormal-operand");
  case EXCEPTION_FLT_DIVIDE_BY_ZERO:    return string_lit("float-divide-by-zero");
  case EXCEPTION_FLT_INEXACT_RESULT:    return string_lit("float-inexact-result");
  case EXCEPTION_FLT_INVALID_OPERATION: return string_lit("float-invalid-operation");
  case EXCEPTION_FLT_OVERFLOW:          return string_lit("float-overflow");
  case EXCEPTION_FLT_STACK_CHECK:       return string_lit("float-stack-check");
  case EXCEPTION_FLT_UNDERFLOW:         return string_lit("float-underflow");
  case EXCEPTION_ILLEGAL_INSTRUCTION:   return string_lit("illegal-instruction");
  case EXCEPTION_IN_PAGE_ERROR:         return string_lit("page-error");
  case EXCEPTION_INT_DIVIDE_BY_ZERO:    return string_lit("integer-divide-by-zero");
  case EXCEPTION_INT_OVERFLOW:          return string_lit("integer-overflow");
  case EXCEPTION_STACK_OVERFLOW:        return string_lit("stack-overflow");
  default:                              return string_lit("unknown");
  }
  // clang-format on
}

/**
 * Retrieve the memory addr associated with the exception (for example the addr of the page error).
 * Returns sentinel_uptr if no address was associated with the exception.
 * TODO: sentinel_uptr (uptr_max) can actually be used; find another sentinel.
 */
INLINE_HINT static uptr diag_except_address(PEXCEPTION_POINTERS exceptCtx) {
  switch (exceptCtx->ExceptionRecord->ExceptionCode) {
  case EXCEPTION_ACCESS_VIOLATION:
    return (uptr)exceptCtx->ExceptionRecord->ExceptionInformation[1];
  case EXCEPTION_IN_PAGE_ERROR:
    return (uptr)exceptCtx->ExceptionRecord->ExceptionInformation[1];
  default:
    return sentinel_uptr;
  }
}

static long SYS_DECL diag_exception_handler(PEXCEPTION_POINTERS exceptCtx) {
  jmp_buf* anchor = g_exceptAnchor;
  g_exceptAnchor  = null; // Clear anchor to avoid triggering it multiple times.

  const i32 code = (i32)exceptCtx->ExceptionRecord->ExceptionCode;
  if (!diag_except_handle(code)) {
    return EXCEPTION_CONTINUE_SEARCH; // This is not an exception we care about, keep searching.
  }

  if (anchor) {
    /**
     * An exception occurred and we have an handler. To report the crash we collect a stack-trace
     * while the offending call-chain is still on the stack and then jump to the anchor for
     * reporting the crash.
     */
    g_exceptStack = symbol_stack_walk();
    g_exceptAddr  = diag_except_address(exceptCtx);
    longjmp(*anchor, code); // Jump to the anchor, will call 'diag_except_enable()' again.
  } else {
    /**
     * No anchor was configured for this thread so we cannot report the crash. In this case we
     * execute the default behavior.
     */
    return EXCEPTION_EXECUTE_HANDLER;
  }
}

void diag_pal_except_enable(jmp_buf* anchor, const i32 exceptCode) {
  static i32 g_exceptHandlerInstalled;

  if (exceptCode) {
    /**
     * An exception has occurred, report the crash with the recorded stack.
     */
    diag_assert(!g_exceptAnchor); // Anchors should be removed when an exception occurs.

    DynString msg = dynstring_create_over(mem_stack(128));
    fmt_write(&msg, "Exception: {}\n", fmt_text(diag_except_name(exceptCode)));
    if (!sentinel_check(g_exceptAddr)) {
      fmt_write(&msg, "Address: {}\n", fmt_int(g_exceptAddr, .base = 16, .minDigits = 16));
    }
    diag_crash_report(&g_exceptStack, dynstring_view(&msg));
    diag_pal_crash();
  } else {
    /**
     * Enable exception interception with the new anchor.
     */
    diag_assert_msg(!g_exceptAnchor, "Exception interception was already active for this thread");
    g_exceptAnchor = anchor;

    if (!thread_atomic_exchange_i32(&g_exceptHandlerInstalled, true)) {
      SetUnhandledExceptionFilter(&diag_exception_handler);
    }
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
