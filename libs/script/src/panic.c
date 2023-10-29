#include "core_alloc.h"
#include "core_array.h"
#include "core_diag.h"
#include "script_panic.h"

static const String g_panicTypeStrs[] = {
    string_static("None"),
    string_static("Assertion failed"),
    string_static("Execution limit exceeded"),
    string_static("Invalid argument"),
    string_static("Missing argument"),
    string_static("Invalid enum entry"),
};
ASSERT(array_elems(g_panicTypeStrs) == ScriptPanicType_Count, "Incorrect number of type strs");

bool script_panic_valid(const ScriptPanic* panic) { return panic->type != ScriptPanic_None; }

String script_panic_type_str(const ScriptPanicType type) {
  diag_assert(type < ScriptPanicType_Count);
  return g_panicTypeStrs[type];
}

void script_panic_pretty_write(DynString* out, const String sourceText, const ScriptPanic* panic) {
  diag_assert(panic->type != ScriptPanic_None && panic->type < ScriptPanicType_Count);

  const ScriptRangeLineCol rangeLineCol = script_range_to_line_col(sourceText, panic->range);
  fmt_write(
      out,
      "{}:{}-{}:{}: {}",
      fmt_int(rangeLineCol.start.line + 1),
      fmt_int(rangeLineCol.start.column + 1),
      fmt_int(rangeLineCol.end.line + 1),
      fmt_int(rangeLineCol.end.column + 1),
      fmt_text(g_panicTypeStrs[panic->type]));
}

String script_panic_pretty_scratch(const String sourceText, const ScriptPanic* diag) {
  Mem       bufferMem = alloc_alloc(g_alloc_scratch, usize_kibibyte, 1);
  DynString buffer    = dynstring_create_over(bufferMem);

  script_panic_pretty_write(&buffer, sourceText, diag);

  return dynstring_view(&buffer);
}
