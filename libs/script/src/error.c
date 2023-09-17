#include "core_array.h"
#include "core_diag.h"
#include "script_error.h"

static const String g_errorStrs[] = {
    string_static("Invalid character"),
    string_static("Invalid Utf8 text"),
    string_static("Key cannot be empty"),
    string_static("String is not terminated"),
    string_static("Recursion limit exceeded"),
    string_static("Variable limit exceeded"),
    string_static("Variable identifier missing"),
    string_static("Variable identifier conflicts"),
    string_static("Missing primary expression"),
    string_static("Invalid primary expression"),
    string_static("No variable found for the given identifier"),
    string_static("No function found for the given identifier"),
    string_static("Incorrect argument count for builtin function"),
    string_static("Unclosed parenthesized expression"),
    string_static("Unterminated scope"),
    string_static("Unterminated argument list"),
    string_static("Block size exceeds maximum"),
    string_static("Argument count exceeds maximum"),
    string_static("Invalid condition count"),
    string_static("Missing colon in select expression"),
    string_static("Unexpected token after expression"),
};

ASSERT(array_elems(g_errorStrs) == ScriptError_Count, "Incorrect number of ScriptError strings");

String script_error_str(const ScriptError error) {
  diag_assert(error < ScriptError_Count);
  return g_errorStrs[error];
}
