#include "core_file.h"
#include "core_math.h"
#include "core_path.h"
#include "core_symbol.h"
#include "core_thread.h"

#include "diag_internal.h"

static THREAD_LOCAL bool          g_diagIsReporting;
static THREAD_LOCAL AssertHandler g_assertHandler;
static THREAD_LOCAL void*         g_assertHandlerContext;
static CrashHandler               g_crashHandler;
static void*                      g_crashHandlerContext;

INLINE_HINT NORETURN static void diag_crash_internal(const String msg) {
  const SymbolStack stack = symbol_stack_walk();
  diag_crash_report(&stack, msg);

  diag_pal_break();
  diag_pal_crash();
}

NO_INLINE_HINT void diag_crash_report(const SymbolStack* stack, const String msg) {
  if (g_diagIsReporting) {
    return; // Avoid reporting crashes that occur during this function.
  }
  g_diagIsReporting = true;

  // Build the text.
  DynString str = dynstring_create_over(mem_stack(2048));
  dynstring_append(&str, string_slice(msg, 0, math_min(msg.size, 512)));
  symbol_stack_write(stack, &str);

  // Write it to stderr.
  file_write_sync(g_fileStdErr, dynstring_view(&str));

  // Write it to a crash-file.
  static ThreadSpinLock g_crashFileLock;
  static bool           g_crashFileWritten;
  thread_spinlock_lock(&g_crashFileLock);
  {
    // NOTE: Only write a single crash-file, even if multiple threads crash.
    if (!g_crashFileWritten) {
      g_crashFileWritten = true;

      const String crashFilePath = path_build_scratch(
          path_parent(g_pathExecutable),
          string_lit("logs"),
          path_name_timestamp_scratch(path_stem(g_pathExecutable), string_lit("crash")));
      file_write_to_path_sync(crashFilePath, dynstring_view(&str));
    }
  }
  thread_spinlock_unlock(&g_crashFileLock);

  g_diagIsReporting = false;
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
