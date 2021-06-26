#include "core_diag.h"
#include "core_file.h"

// Forward declare libc exit(code).
void exit(int);

void diag_print_raw(String msg) { file_write_sync(g_file_stdout, msg); }

void diag_print_err_raw(String msg) { file_write_sync(g_file_stderr, msg); }

void diag_assert_fail_raw(String msg, const SourceLoc sourceLoc) {
  diag_print_err(
      "Assertion failed: '{}' [file: {} line: {}]\n",
      fmt_text(msg),
      fmt_path(sourceLoc.file),
      fmt_int(sourceLoc.line));
  diag_crash();
}

void diag_crash() {
  DEBUG_BREAK();
  exit(1);
}
