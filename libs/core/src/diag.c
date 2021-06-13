#include "core_diag.h"
#include "core_file.h"

// Forward declare libc exit(code).
void exit(int);

static void diag_write_sync(File* file, String format, const FormatArg* args) {
  file_write_sync(file, format_write_formatted_scratch(format, args));
}

void diag_print_formatted(String format, const FormatArg* args) {
  diag_write_sync(g_file_stdout, format, args);
}

void diag_print_err_formatted(String format, const FormatArg* args) {
  diag_write_sync(g_file_stderr, format, args);
}

void diag_assert_fail(const DiagCallSite* callsite, String msg) {
  diag_print_err(
      "Assertion failed: '{}' [file: {} line: {}]\n",
      fmt_text(msg),
      fmt_path(callsite->file),
      fmt_int(callsite->line));
  diag_crash();
}

void diag_crash() {
  DEBUG_BREAK();
  exit(1);
}
