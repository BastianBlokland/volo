#pragma once
#include "script_doc.h"

// Forward declare from 'core_dynstring.h'.
typedef struct sDynArray DynString;

typedef enum {
  ScriptCompileResult_Success,
  ScriptCompileResult_TooManyValues,

  ScriptCompileResult_Count,
} ScriptCompileResult;

String script_compile_result_str(ScriptCompileResult);

/**
 * Compile an expression to byte-code for that can be executed in the vm.
 * Output is written to the given DynString.
 */
ScriptCompileResult script_compile(const ScriptDoc*, ScriptExpr, DynString* out);
