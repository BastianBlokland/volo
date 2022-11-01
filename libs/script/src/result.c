#include "core_diag.h"
#include "script_result.h"

String script_result_str(const ScriptResult result) {
  diag_assert(result < 2);
  return result == ScriptResult_Success ? string_lit("success") : string_lit("fail");
}
