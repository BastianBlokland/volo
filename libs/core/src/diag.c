#include "core_file.h"
#include "core_math.h"
#include "core_path.h"
#include "core_symbol.h"
#include "core_thread.h"

#include "diag_internal.h"

static THREAD_LOCAL AssertHandler g_assertHandler;
static THREAD_LOCAL void*         g_assertHandlerContext;

static CrashHandler g_crashHandler;
static void*        g_crashHandlerContext;

INLINE_HINT NORETURN static void diag_crash_internal(const String msg) {
  const SymbolStack stack = symbol_stack_walk();
  diag_crash_report(&stack, msg);

  diag_pal_break();
  diag_pal_crash();
}

NO_INLINE_HINT static void diag_crash_file_write(const String text) {
  DynString filePath = dynstring_create_over(mem_stack(1024));
  path_build(
      &filePath,
      path_parent(g_pathExecutable),
      string_lit("logs"),
      path_name_timestamp_scratch(path_stem(g_pathExecutable), string_lit("crash")));

  file_write_to_path_sync(dynstring_view(&filePath), text);
}

NO_INLINE_HINT void diag_crash_report(const SymbolStack* stack, const String msg) {
  static THREAD_LOCAL bool g_crashBusy;
  if (g_crashBusy) {
    return; // Avoid reporting crashes that occur during this function.
  }
  g_crashBusy = true;

  /**
   * Report the crash to the stderr stream.
   * NOTE: There is no locking up to this point so when multiple threads crash at the same time then
   * all their crashes are written to stderr.
   */

  DynString str = dynstring_create_over(mem_stack(2048));
  dynstring_append(&str, string_slice(msg, 0, math_min(msg.size, 512)));
  symbol_stack_write(stack, &str);

  file_write_sync(g_fileStdErr, dynstring_view(&str));

  /**
   * Write a crash-file and invoke any user crash-handler (if registered).
   * NOTE: Only runs for the first thread that crashes, the other threads will block until the
   * reporting is done.
   * TODO: Use a Mutex instead of SpinLock to avoid wasting resources while we are inside the user
   * crash-handler (which could be displaying a modal popup for example).
   */

  static ThreadSpinLock g_crashLock;
  static bool           g_crashReported;
  thread_spinlock_lock(&g_crashLock);
  {
    if (!g_crashReported) {
      g_crashReported = true;

      diag_crash_file_write(dynstring_view(&str));

      if (g_crashHandler) {
        g_crashHandler(dynstring_view(&str), g_crashHandlerContext);
      }
    }
  }
  thread_spinlock_unlock(&g_crashLock);
  g_crashBusy = false;
}

void diag_print_raw(const String userMsg) { file_write_sync(g_fileStdOut, userMsg); }
void diag_print_err_raw(const String userMsg) { file_write_sync(g_fileStdErr, userMsg); }

void diag_assert_report_fail(const String userMsg, const SourceLoc sourceLoc) {
  if (g_assertHandler && g_assertHandler(userMsg, sourceLoc, g_assertHandlerContext)) {
    return; // Assert was handled.
  }
  const String msg = fmt_write_scratch(
      "Assertion failed: '{}' [file: {} line: {}]\n",
      fmt_text(userMsg),
      fmt_path(sourceLoc.file),
      fmt_int(sourceLoc.line));
  diag_crash_internal(msg);
}

void diag_break(void) { diag_pal_break(); }
void diag_crash(void) { diag_crash_internal(string_lit("Crash: Unknown error\n")); }

void diag_crash_msg_raw(const String userMsg) {
  const String msg = fmt_write_scratch("Crash: {}\n", fmt_text(userMsg));
  diag_crash_internal(msg);
}

void diag_assert_handler(AssertHandler handler, void* context) {
  g_assertHandler        = handler;
  g_assertHandlerContext = context;
}

void diag_crash_handler(CrashHandler handler, void* context) {
  g_crashHandler        = handler;
  g_crashHandlerContext = context;
}

void diag_except_enable(jmp_buf* anchor, const i32 exceptionCode) {
  diag_pal_except_enable(anchor, exceptionCode);
}

void diag_except_disable(void) { diag_pal_except_disable(); }
