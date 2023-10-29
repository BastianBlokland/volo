#include "core_diag.h"
#include "script_error.h"
#include "script_panic.h"

ScriptPanicType script_error_to_panic(const ScriptErrorType type) {
  switch (type) {
  case ScriptError_None:
    return ScriptPanic_None;
  case ScriptError_InvalidValue:
    return ScriptPanic_InvalidValue;
  }
  diag_assert_fail("Invalid script error type");
  UNREACHABLE
}
