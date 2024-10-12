#pragma once
#include "script_doc.h"

// Forward declare from 'core_dynstring.h'.
typedef struct sDynArray DynString;

typedef enum {
  ScriptCompileError_None,
  ScriptCompileError_TooManyRegisters,
  ScriptCompileError_TooManyValues,
  ScriptCompileError_CodeLimitExceeded,

  ScriptCompileError_Count,
} ScriptCompileError;

String script_compile_error_str(ScriptCompileError);

/**
 * Compile an expression to byte-code that can be executed in the vm.
 * Output is written to the given DynString.
 */
ScriptCompileError script_compile(const ScriptDoc*, ScriptExpr, DynString* out);
