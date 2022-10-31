#pragma once
#include "script_doc.h"

// Forward declare from 'script_mem.h'.
typedef struct sScriptMem ScriptMem;

/**
 * Evaluate the given expression.
 */
ScriptVal script_eval(const ScriptDoc*, ScriptMem*, ScriptExpr);
