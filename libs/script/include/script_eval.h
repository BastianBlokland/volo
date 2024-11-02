#pragma once
#include "script_doc.h"
#include "script_panic.h"

// Forward declare from 'script_mem.h'.
typedef struct sScriptMem ScriptMem;

// Forward declare from 'script_binder.h'.
typedef struct sScriptBinder ScriptBinder;

// Forward declare from 'script_pos.h'.
typedef struct sScriptLookup ScriptLookup;

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
