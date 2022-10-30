#include "core_array.h"
#include "core_diag.h"
#include "script_error.h"

static const String g_errorStrs[] = {
    string_static("InvalidChar"),
    string_static("InvalidCharInNull"),
    string_static("InvalidCharInTrue"),
    string_static("InvalidCharInFalse"),
    string_static("KeyIdentifierEmpty"),
    string_static("KeyIdentifierInvalidUtf8"),
    string_static("RecursionLimitExceeded"),
    string_static("MissingPrimaryExpression"),
    string_static("InvalidPrimaryExpression"),
};

ASSERT(array_elems(g_errorStrs) == ScriptError_Count, "Incorrect number of ScriptError strings");

String script_error_str(const ScriptError error) {
  diag_assert(error < ScriptError_Count);
  return g_errorStrs[error];
}
