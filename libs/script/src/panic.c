#include "core_alloc.h"
#include "core_array.h"
#include "core_diag.h"
#include "script_panic.h"

static const String g_panicKindStrs[] = {
    string_static("None"),
    string_static("Assertion failed"),
    string_static("Execution limit exceeded"),
    string_static("Argument invalid"),
    string_static("Argument is null"),
    string_static("Argument missing"),
    string_static("Argument out of range"),
    string_static("Argument count exceeds maximum"),
    string_static("Invalid enum entry"),
    string_static("Unimplemented binding"),
    string_static("Query limit exceeded"),
    string_static("Query invalid"),
    string_static("Cannot change readonly parameter"),
    string_static("Required capability is missing"),
};
ASSERT(array_elems(g_panicKindStrs) == ScriptPanicKind_Count, "Incorrect number of kind strs");

bool script_panic_valid(const ScriptPanic* panic) { return panic->kind != ScriptPanic_None; }

String script_panic_kind_str(const ScriptPanicKind kind) {
  diag_assert(kind < ScriptPanicKind_Count);
  return g_panicKindStrs[kind];
}

void script_panic_pretty_write(DynString* out, const String sourceText, const ScriptPanic* panic) {
  diag_assert(panic->kind != ScriptPanic_None && panic->kind < ScriptPanicKind_Count);

  const ScriptRangeLineCol rangeLineCol = script_range_to_line_col(sourceText, panic->range);
  fmt_write(
      out,
      "{}:{}-{}:{}: {}",
      fmt_int(rangeLineCol.start.line + 1),
      fmt_int(rangeLineCol.start.column + 1),
      fmt_int(rangeLineCol.end.line + 1),
      fmt_int(rangeLineCol.end.column + 1),
      fmt_text(g_panicKindStrs[panic->kind]));
}

String script_panic_pretty_scratch(const String sourceText, const ScriptPanic* diag) {
  Mem       bufferMem = alloc_alloc(g_allocScratch, usize_kibibyte, 1);
  DynString buffer    = dynstring_create_over(bufferMem);

  script_panic_pretty_write(&buffer, sourceText, diag);

  return dynstring_view(&buffer);
}
