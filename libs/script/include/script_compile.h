#pragma once
#include "script.h"

typedef enum eScriptCompileError {
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
ScriptCompileError
script_compile(const ScriptDoc*, const ScriptLookup*, ScriptExpr, Allocator*, ScriptProgram* out);
