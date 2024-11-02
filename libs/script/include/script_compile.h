#pragma once
#include "core_string.h"

// Forward declare from 'script_prog.h'
typedef struct sScriptProgram ScriptProgram;

// Forward declare from 'script_doc.h'
typedef struct sScriptDoc ScriptDoc;
typedef u32               ScriptExpr;

// Forward declare from 'script_pos.h'.
typedef struct sScriptLookup ScriptLookup;

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
ScriptCompileError
script_compile(const ScriptDoc*, const ScriptLookup*, ScriptExpr, Allocator*, ScriptProgram* out);
