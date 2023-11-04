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

ScriptPanicKind script_error_to_panic(const ScriptErrorKind kind) {
  switch (kind) {
  case ScriptError_None:
    return ScriptPanic_None;
  case ScriptError_ArgumentInvalid:
    return ScriptPanic_ArgumentInvalid;
  case ScriptError_ArgumentMissing:
    return ScriptPanic_ArgumentMissing;
  case ScriptError_ArgumentOutOfRange:
    return ScriptPanic_ArgumentOutOfRange;
  case ScriptError_EnumInvalidEntry:
    return ScriptPanic_EnumInvalidEntry;
  }
  diag_assert_fail("Invalid script error kind");
  UNREACHABLE
}
