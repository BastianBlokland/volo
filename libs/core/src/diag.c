#include "core_diag.h"
#include "core_file.h"

// Forward declare libc exit(code).
void exit(int);

static void diag_write_sync(File* file, String format, const FormatArg* args, usize argsCount) {
  DynString buffer = dynstring_create_over(mem_stack(1 * usize_kibibyte));

  format_write_formatted(&buffer, format, args, argsCount);

  file_write_sync(file, dynstring_view(&buffer));
  dynstring_destroy(&buffer);
}

void diag_log_formatted(String format, const FormatArg* args, usize argsCount) {
  diag_write_sync(g_file_stdout, format, args, argsCount);
}

void diag_log_err_formatted(String format, const FormatArg* args, usize argsCount) {
  diag_write_sync(g_file_stderr, format, args, argsCount);
}

void diag_assert_fail(const DiagCallSite* callsite, String msg) {
  diag_log_err(
      "Assertion failed: '{}' [file: {} line: {}]\n",
      fmt_text(msg),
      fmt_text(callsite->file),
      fmt_int(callsite->line));
  diag_crash();
}

void diag_crash() {
  VOLO_DEBUG_BREAK();
  exit(1);
}
