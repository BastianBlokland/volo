#include "core_alloc.h"
#include "core_array.h"
#include "core_diag.h"
#include "script_panic.h"

static const String g_panicStrs[] = {
    [ScriptPanic_None]                        = string_static("None"),
    [ScriptPanic_AssertionFailed]             = string_static("Assertion failed"),
    [ScriptPanic_ExecutionFailed]             = string_static("Execution failed"),
    [ScriptPanic_ExecutionLimitExceeded]      = string_static("Execution limit exceeded"),
    [ScriptPanic_ArgumentInvalid]             = string_static("Argument {arg-index} invalid"),
    [ScriptPanic_ArgumentNull]                = string_static("Argument {arg-index} is null"),
    [ScriptPanic_ArgumentMissing]             = string_static("Argument {arg-index} missing"),
    [ScriptPanic_ArgumentOutOfRange]          = string_static("Argument {arg-index} out of range"),
    [ScriptPanic_ArgumentCountExceedsMaximum] = string_static("Argument count exceeds maximum"),
    [ScriptPanic_EnumInvalidEntry]            = string_static("Invalid enum entry"),
    [ScriptPanic_UnimplementedBinding]        = string_static("Unimplemented binding"),
    [ScriptPanic_QueryLimitExceeded]          = string_static("Query limit exceeded"),
    [ScriptPanic_QueryInvalid]                = string_static("Query invalid"),
    [ScriptPanic_ReadonlyParam]               = string_static("Cannot change readonly parameter"),
    [ScriptPanic_MissingCapability]           = string_static("Required capability is missing"),
};
ASSERT(array_elems(g_panicStrs) == ScriptPanicKind_Count, "Incorrect number of kind strs");

typedef enum {
  PanicReplKind_ArgIndex,
} PanicReplKind;

typedef struct {
  usize         start, end;
  PanicReplKind kind;
} PanicRepl;

static PanicReplKind panic_replacement_parse(const String str) {
  if (string_eq(str, string_lit("arg-index"))) {
    return PanicReplKind_ArgIndex;
  }
  diag_crash_msg("Unsupported panic replacement");
}

static bool panic_replacement_find(String str, PanicRepl* result) {
  const usize startIdx = string_find_first_char(str, '{');
  if (sentinel_check(startIdx)) {
    return false;
  }
  const usize len = string_find_first_char(mem_consume(str, startIdx), '}');
  diag_assert(!sentinel_check(len));

  *result = (PanicRepl){
      .start = startIdx,
      .end   = startIdx + len + 1,
      .kind  = panic_replacement_parse(string_slice(str, startIdx + 1, len - 1)),
  };
  return true;
}

void script_panic_write(
    DynString* out, const ScriptPanic* panic, const ScriptPanicOutputFlags flags) {
  diag_assert(panic->kind != ScriptPanic_None && panic->kind < ScriptPanicKind_Count);

  if (flags & ScriptPanicOutput_IncludeRange) {
    fmt_write(
        out,
        "{}:{}-{}:{}: ",
        fmt_int(panic->range.start.line + 1),
        fmt_int(panic->range.start.column + 1),
        fmt_int(panic->range.end.line + 1),
        fmt_int(panic->range.end.column + 1));
  }

  String str = g_panicStrs[panic->kind];
  while (!string_is_empty(str)) {
    PanicRepl repl;
    if (!panic_replacement_find(str, &repl)) {
      // No replacement, append the text verbatim.
      dynstring_append(out, str);
      break;
    }

    // Append the text before the replacement verbatim.
    dynstring_append(out, string_slice(str, 0, repl.start));

    switch (repl.kind) {
    case PanicReplKind_ArgIndex:
      format_write_int(out, panic->argIndex);
      break;
    }

    str = mem_consume(str, repl.end);
  }
}

String script_panic_scratch(const ScriptPanic* diag, const ScriptPanicOutputFlags flags) {
  Mem       bufferMem = alloc_alloc(g_allocScratch, usize_kibibyte, 1);
  DynString buffer    = dynstring_create_over(bufferMem);

  script_panic_write(&buffer, diag, flags);

  return dynstring_view(&buffer);
}
