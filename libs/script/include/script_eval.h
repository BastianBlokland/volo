#pragma once
#include "script_doc.h"

// Forward declare from 'script_mem.h'.
typedef struct sScriptMem ScriptMem;

// Forward declare from 'core_binder.h'.
typedef struct sScriptBinder ScriptBinder;

/**
 * Evaluate the given expression.
 */
ScriptVal script_eval(const ScriptDoc*, ScriptMem*, ScriptExpr, const ScriptBinder*, void* bindCtx);
ScriptVal script_eval_readonly(const ScriptDoc*, const ScriptMem*, ScriptExpr);
