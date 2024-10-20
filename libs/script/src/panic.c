#include "core_alloc.h"
#include "core_array.h"
#include "core_diag.h"
#include "script_panic.h"

static const String g_panicKindStrs[] = {
    [ScriptPanic_None]                        = string_static("None"),
    [ScriptPanic_AssertionFailed]             = string_static("Assertion failed"),
    [ScriptPanic_ExecutionFailed]             = string_static("Execution failed"),
    [ScriptPanic_ExecutionLimitExceeded]      = string_static("Execution limit exceeded"),
    [ScriptPanic_ArgumentInvalid]             = string_static("Argument invalid"),
    [ScriptPanic_ArgumentNull]                = string_static("Argument is null"),
    [ScriptPanic_ArgumentMissing]             = string_static("Argument missing"),
    [ScriptPanic_ArgumentOutOfRange]          = string_static("Argument out of range"),
    [ScriptPanic_ArgumentCountExceedsMaximum] = string_static("Argument count exceeds maximum"),
    [ScriptPanic_EnumInvalidEntry]            = string_static("Invalid enum entry"),
    [ScriptPanic_UnimplementedBinding]        = string_static("Unimplemented binding"),
    [ScriptPanic_QueryLimitExceeded]          = string_static("Query limit exceeded"),
    [ScriptPanic_QueryInvalid]                = string_static("Query invalid"),
    [ScriptPanic_ReadonlyParam]               = string_static("Cannot change readonly parameter"),
    [ScriptPanic_MissingCapability]           = string_static("Required capability is missing"),
};
ASSERT(array_elems(g_panicKindStrs) == ScriptPanicKind_Count, "Incorrect number of kind strs");

bool script_panic_valid(const ScriptPanic* panic) { return panic->kind != ScriptPanic_None; }

String script_panic_kind_str(const ScriptPanicKind kind) {
  diag_assert(kind < ScriptPanicKind_Count);
  return g_panicKindStrs[kind];
}

void script_panic_pretty_write(DynString* out, const ScriptPanic* panic) {
  diag_assert(panic->kind != ScriptPanic_None && panic->kind < ScriptPanicKind_Count);

  fmt_write(
      out,
      "{}:{}-{}:{}: {}",
      fmt_int(panic->range.start.line + 1),
      fmt_int(panic->range.start.column + 1),
      fmt_int(panic->range.end.line + 1),
      fmt_int(panic->range.end.column + 1),
      fmt_text(g_panicKindStrs[panic->kind]));
}

String script_panic_pretty_scratch(const ScriptPanic* diag) {
  Mem       bufferMem = alloc_alloc(g_allocScratch, usize_kibibyte, 1);
  DynString buffer    = dynstring_create_over(bufferMem);

  script_panic_pretty_write(&buffer, diag);

  return dynstring_view(&buffer);
}
