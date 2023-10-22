#include "core_array.h"
#include "core_diag.h"
#include "script_error.h"

static const String g_errorRuntimeStrs[] = {
    string_static("Success"),
    string_static("Assertion failed"),
    string_static("Execution limit exceeded"),
};
ASSERT(array_elems(g_errorRuntimeStrs) == ScriptErrorRuntime_Count, "Incorrect number of err strs");

String script_error_runtime_str(const ScriptErrorRuntime error) {
  diag_assert(error < ScriptErrorRuntime_Count);
  return g_errorRuntimeStrs[error];
}
