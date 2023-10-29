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
  case ScriptError_InvalidArgument:
    return ScriptPanic_InvalidArgument;
  case ScriptError_MissingArgument:
    return ScriptPanic_MissingArgument;
  case ScriptError_InvalidEnumEntry:
    return ScriptPanic_InvalidEnumEntry;
  }
  diag_assert_fail("Invalid script error type");
  UNREACHABLE
}
