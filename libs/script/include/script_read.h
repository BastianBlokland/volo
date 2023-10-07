#pragma once
#include "script_doc.h"
#include "script_result.h"

// Forward declare from 'core_binder.h'.
typedef struct sScriptBinder ScriptBinder;

// Forward declare from 'core_diag.h'.
typedef struct sScriptDiagBag ScriptDiagBag;

/**
 * Result of parsing a script expression.
 * If 'type == ScriptResult_Success' then 'expr' contains an expression in the provided ScriptDoc.
 * else the error information is populated.
 */
typedef struct {
  ScriptResult type;
  union {
    ScriptExpr expr;
  };
} ScriptReadResult;

/**
 * Read a script expression.
 *
 * Pre-condition: res != null.
 */
void script_read(ScriptDoc*, const ScriptBinder*, String, ScriptDiagBag*, ScriptReadResult* res);
