#pragma once
#include "script_doc.h"
#include "script_error.h"

// Forward declare from 'script_mem.h'.
typedef struct sScriptMem ScriptMem;

// Forward declare from 'core_binder.h'.
typedef struct sScriptBinder ScriptBinder;

typedef struct {
  ScriptError error;
  ScriptVal   val;
} ScriptEvalResult;

/**
 * Evaluate the given expression.
 */

// clang-format off
ScriptEvalResult script_eval(const ScriptDoc*, ScriptMem*, ScriptExpr, const ScriptBinder*, void* bindCtx);
ScriptEvalResult script_eval_readonly(const ScriptDoc*, const ScriptMem*, ScriptExpr);
// clang-format on
