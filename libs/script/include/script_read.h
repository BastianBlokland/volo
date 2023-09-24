#pragma once
#include "script_doc.h"
#include "script_result.h"

// Forward declare from 'core_binder.h'.
typedef struct sScriptBinder ScriptBinder;

typedef struct {
  u16 line, column;
} ScriptPos;

/**
 * Result of parsing a script expression.
 * If 'type == ScriptResult_Success' then 'expr' contains an expression in the provided ScriptDoc.
 * else the error information is populated.
 */
typedef struct {
  ScriptResult type;
  union {
    ScriptExpr expr;
    struct {
      ScriptPos errorStart, errorEnd;
    };
  };
} ScriptReadResult;

/**
 * Read a script expression.
 *
 * Pre-condition: res != null.
 */
void script_read(ScriptDoc*, const ScriptBinder*, String, ScriptReadResult* res);

/**
 * Create a textual representation of the result.
 */
void   script_read_result_write(DynString*, const ScriptDoc*, const ScriptReadResult*);
String script_read_result_scratch(const ScriptDoc*, const ScriptReadResult*);
