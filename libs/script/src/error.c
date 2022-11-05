#include "core_array.h"
#include "core_diag.h"
#include "script_error.h"

static const String g_errorStrs[] = {
    string_static("InvalidChar"),
    string_static("InvalidUtf8"),
    string_static("KeyEmpty"),
    string_static("RecursionLimitExceeded"),
    string_static("MissingPrimaryExpression"),
    string_static("InvalidPrimaryExpression"),
    string_static("NoConstantFoundForIdentifier"),
    string_static("NoFunctionFoundForIdentifier"),
    string_static("UnclosedParenthesizedExpression"),
    string_static("UnterminatedArgumentList"),
    string_static("ArgumentCountExceedsMaximum"),
    string_static("UnexpectedTokenAfterExpression"),
};

ASSERT(array_elems(g_errorStrs) == ScriptError_Count, "Incorrect number of ScriptError strings");

String script_error_str(const ScriptError error) {
  diag_assert(error < ScriptError_Count);
  return g_errorStrs[error];
}
