#pragma once
#include "script.h"
#include "script_panic.h"
#include "script_val.h"

typedef struct {
  u32         executedOps;
  ScriptPanic panic;
  ScriptVal   val;
} ScriptEvalResult;

/**
 * Evaluate the given expression.
 */
ScriptEvalResult script_eval(
    const ScriptDoc*,
    const ScriptLookup*, // [Optional] Used to lookup source locations.
    ScriptExpr,
    ScriptMem*,          // [Optional] Memory access provider.
    const ScriptBinder*, // [Optional] External function binder.
    void* bindCtx);
