#include "core_alloc.h"
#include "core_diag.h"
#include "core_format.h"
#include "script_diag.h"

void script_diag_write(DynString* out, const String sourceText, const ScriptDiag* diag) {
  diag_assert(diag->error != ScriptResult_Success);

  const ScriptPosLineCol rangeStart = script_pos_to_line_col(sourceText, diag->range.start);
  const ScriptPosLineCol rangeEnd   = script_pos_to_line_col(sourceText, diag->range.end);
  fmt_write(
      out,
      "{}:{}-{}:{}: {}",
      fmt_int(rangeStart.line + 1),
      fmt_int(rangeStart.column + 1),
      fmt_int(rangeEnd.line + 1),
      fmt_int(rangeEnd.column + 1),
      fmt_text(script_result_str(diag->error)));
}

String script_diag_scratch(const String sourceText, const ScriptDiag* diag) {
  diag_assert(diag->error != ScriptResult_Success);

  Mem       bufferMem = alloc_alloc(g_alloc_scratch, usize_kibibyte, 1);
  DynString buffer    = dynstring_create_over(bufferMem);

  script_diag_write(&buffer, sourceText, diag);

  return dynstring_view(&buffer);
}
