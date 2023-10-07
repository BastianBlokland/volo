#include "core_alloc.h"
#include "core_diag.h"
#include "core_format.h"
#include "script_diag.h"

void script_diag_write(DynString* out, const String sourceText, const ScriptDiag* diag) {
  diag_assert(diag->error != ScriptResult_Success);

  const ScriptPosHuman humanPosStart = script_pos_humanize(sourceText, diag->range.start);
  const ScriptPosHuman humanPosEnd   = script_pos_humanize(sourceText, diag->range.end);
  fmt_write(
      out,
      "{}:{}-{}:{}: {}",
      fmt_int(humanPosStart.line),
      fmt_int(humanPosStart.column),
      fmt_int(humanPosEnd.line),
      fmt_int(humanPosEnd.column),
      fmt_text(script_result_str(diag->error)));
}

String script_diag_scratch(const String sourceText, const ScriptDiag* diag) {
  diag_assert(diag->error != ScriptResult_Success);

  Mem       bufferMem = alloc_alloc(g_alloc_scratch, usize_kibibyte, 1);
  DynString buffer    = dynstring_create_over(bufferMem);

  script_diag_write(&buffer, sourceText, diag);

  return dynstring_view(&buffer);
}
