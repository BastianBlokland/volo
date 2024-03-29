#include "core_diag.h"
#include "core_file.h"

#include "diag_internal.h"

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

void diag_break() { diag_pal_break(); }

void diag_crash() {
  diag_break();
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
