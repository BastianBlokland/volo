#include "core_alloc.h"
#include "core_diag.h"
#include "core_format.h"
#include "script_diag.h"

bool script_diag_push(ScriptDiagBag* bag, const ScriptDiag* diag) {
  if (UNLIKELY(bag->count == script_diag_max)) {
    return false;
  }
  bag->values[bag->count++] = *diag;
  return true;
}

void script_diag_clear(ScriptDiagBag* bag) { bag->count = 0; }

bool script_diag_any_error(const ScriptDiagBag* bag) {
  return bag->count > 0; // NOTE: All diagnostics are errors at the moment.
}

String script_diag_msg_scratch(const String sourceText, const ScriptDiag* diag) {
  const String rangeText = script_pos_range_text(sourceText, diag->range);

  FormatArg formatArgs[2] = {0};
  if (rangeText.size < 32) {
    formatArgs[0] = fmt_text(rangeText, .flags = FormatTextFlags_EscapeNonPrintAscii);
  }
  return format_write_formatted_scratch(script_error_str(diag->error), formatArgs);
}

void script_diag_write(DynString* out, const String sourceText, const ScriptDiag* diag) {
  diag_assert(diag->error != ScriptError_None);

  const ScriptPosLineCol rangeStart = script_pos_to_line_col(sourceText, diag->range.start);
  const ScriptPosLineCol rangeEnd   = script_pos_to_line_col(sourceText, diag->range.end);
  fmt_write(
      out,
      "{}:{}-{}:{}: {}",
      fmt_int(rangeStart.line + 1),
      fmt_int(rangeStart.column + 1),
      fmt_int(rangeEnd.line + 1),
      fmt_int(rangeEnd.column + 1),
      fmt_text(script_diag_msg_scratch(sourceText, diag)));
}

String script_diag_scratch(const String sourceText, const ScriptDiag* diag) {
  diag_assert(diag->error != ScriptError_None);

  Mem       bufferMem = alloc_alloc(g_alloc_scratch, usize_kibibyte, 1);
  DynString buffer    = dynstring_create_over(bufferMem);

  script_diag_write(&buffer, sourceText, diag);

  return dynstring_view(&buffer);
}
