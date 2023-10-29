#include "core_diag.h"
#include "script_error.h"
#include "script_panic.h"

ScriptError script_error(const ScriptErrorType type) {
  return (ScriptError){
      .type     = type,
      .argIndex = script_error_arg_sentinel,
  };
}

ScriptError script_error_arg(const ScriptErrorType type, const u16 argIndex) {
  return (ScriptError){
      .type     = type,
      .argIndex = argIndex,
  };
}

ScriptPanicType script_error_to_panic(const ScriptErrorType type) {
  switch (type) {
  case ScriptError_None:
    return ScriptPanic_None;
  case ScriptError_ArgumentInvalid:
    return ScriptPanic_ArgumentInvalid;
  case ScriptError_ArgumentMissing:
    return ScriptPanic_ArgumentMissing;
  case ScriptError_EnumInvalidEntry:
    return ScriptPanic_EnumInvalidEntry;
  }
  diag_assert_fail("Invalid script error type");
  UNREACHABLE
}
