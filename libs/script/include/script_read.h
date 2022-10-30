#pragma once
#include "script_doc.h"
#include "script_error.h"
#include "script_result.h"

/**
 * Result of parsing a script expression.
 * If 'type == ScriptResult_Success' then 'expr' contains an expression in the provided ScriptDoc.
 * else 'error' contains the reason why parsing failed.
 */
typedef struct {
  ScriptResult type;
  union {
    ScriptExpr  expr;
    ScriptError error;
  };
} ScriptReadResult;

/**
 * Read a script expression.
 *
 * Returns the remaining input.
 * The result is written to the output pointer.
 *
 * Pre-condition: res != null.
 */
String script_read_expr(ScriptDoc*, String, ScriptReadResult* res);
