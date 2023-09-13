#pragma once
#include "script_doc.h"
#include "script_error.h"
#include "script_result.h"

typedef struct {
  u16 line, column;
} ScriptPos;

/**
 * Result of parsing a script expression.
 * If 'type == ScriptResult_Success' then 'expr' contains an expression in the provided ScriptDoc.
 * else 'error' contains the reason why parsing failed.
 */
typedef struct {
  ScriptResult type;
  union {
    ScriptExpr expr;
    struct {
      ScriptError error;
      ScriptPos   errorStart, errorEnd;
    };
  };
} ScriptReadResult;

/**
 * Read a script expression.
 *
 * Pre-condition: res != null.
 */
void script_read(ScriptDoc*, String, ScriptReadResult* res);
