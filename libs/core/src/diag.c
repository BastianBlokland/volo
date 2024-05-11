#include "core_diag.h"
#include "core_file.h"
#include "core_symbol.h"

#include "diag_internal.h"

#define VOLO_DIAG_STACK_TRACE

THREAD_LOCAL AssertHandler g_assertHandler;
THREAD_LOCAL void*         g_assertHandlerContext;

static bool assert_handler_print(String msg, const SourceLoc sourceLoc, void* context) {
  (void)msg;
  (void)sourceLoc;
  (void)context;

  diag_print_err(
      "Assertion failed: '{}' [file: {} line: {}]\n",
      fmt_text(msg),
      fmt_path(sourceLoc.file),
      fmt_int(sourceLoc.line));

  return false;
}

void diag_print_raw(String msg) { file_write_sync(g_file_stdout, msg); }

void diag_print_err_raw(String msg) { file_write_sync(g_file_stderr, msg); }

void diag_assert_report_fail(String msg, const SourceLoc sourceLoc) {
  const AssertHandler assertHandler = g_assertHandler ? g_assertHandler : assert_handler_print;
  if (!assertHandler(msg, sourceLoc, g_assertHandlerContext)) {
    diag_crash();
  }
}

void diag_break(void) { diag_pal_break(); }

void diag_crash(void) {
  static THREAD_LOCAL bool g_diagCrashing;
  if (!g_diagCrashing) {
    /**
     * Handle a crash happening while in this function.
     * Can for example happen when an error occurs while resolving stack symbol names.
     */
    g_diagCrashing = true;

#ifdef VOLO_DIAG_STACK_TRACE
    {
      const SymbolStack stack = symbol_stack();

      DynString str = dynstring_create_over(mem_stack(2048));
      symbol_stack_write(&stack, &str);
      file_write_sync(g_file_stderr, dynstring_view(&str));
    }
#endif
    diag_break();
  }
  diag_pal_crash();
}

void diag_crash_msg_raw(String msg) {
  (void)msg;
  diag_print_err("Crash: '{}'\n", fmt_text(msg));
  diag_crash();
}

void diag_set_assert_handler(AssertHandler handler, void* context) {
  g_assertHandler        = handler;
  g_assertHandlerContext = context;
}
