#pragma once
#include "script_doc.h"
#include "script_panic.h"
#include "script_pos.h"

// Forward declare from 'script_mem.h'.
typedef struct sScriptMem ScriptMem;

// Forward declare from 'core_binder.h'.
typedef struct sScriptBinder ScriptBinder;

typedef struct {
  u32         executedExprs;
  ScriptPanic panic;
  ScriptVal   val;
} ScriptEvalResult;

/**
 * Evaluate the given expression.
 */

// clang-format off
ScriptEvalResult script_eval(const ScriptDoc*, ScriptExpr, ScriptMem*, const ScriptBinder*, void* bindCtx);
// clang-format on
