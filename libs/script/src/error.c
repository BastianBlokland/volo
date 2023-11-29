#include "core_diag.h"
#include "script_error.h"
#include "script_panic.h"

ScriptError script_error(const ScriptErrorKind kind) {
  return (ScriptError){
      .kind     = kind,
      .argIndex = script_error_arg_sentinel,
  };
}

ScriptError script_error_arg(const ScriptErrorKind kind, const u16 argIndex) {
  return (ScriptError){
      .kind     = kind,
      .argIndex = argIndex,
  };
}

bool script_error_valid(const ScriptError* error) { return error->kind != ScriptError_None; }

ScriptPanicKind script_error_to_panic(const ScriptErrorKind kind) {
  static ScriptPanicKind g_panics[ScriptErrorKind_Count] = {
      [ScriptError_None]                        = ScriptPanic_None,
      [ScriptError_ArgumentInvalid]             = ScriptPanic_ArgumentInvalid,
      [ScriptError_ArgumentNull]                = ScriptPanic_ArgumentNull,
      [ScriptError_ArgumentMissing]             = ScriptPanic_ArgumentMissing,
      [ScriptError_ArgumentOutOfRange]          = ScriptPanic_ArgumentOutOfRange,
      [ScriptError_ArgumentCountExceedsMaximum] = ScriptPanic_ArgumentCountExceedsMaximum,
      [ScriptError_EnumInvalidEntry]            = ScriptPanic_EnumInvalidEntry,
      [ScriptError_UnimplementedBinding]        = ScriptPanic_UnimplementedBinding,
      [ScriptError_QueryLimitExceeded]          = ScriptPanic_QueryLimitExceeded,
      [ScriptError_QueryInvalid]                = ScriptPanic_QueryInvalid,
  };
  diag_assert(kind < ScriptErrorKind_Count);
  return g_panics[kind];
}
