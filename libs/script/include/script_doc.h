#pragma once
#include "core_dynstring.h"
#include "core_types.h"
#include "script_compare.h"
#include "script_val.h"

// Forward declare from 'core_alloc.h'.
typedef struct sAllocator Allocator;

/**
 * Definition of a Script Document for storing script expressions.
 */
typedef struct sScriptDoc ScriptDoc;

/**
 * Type of a Script expression.
 */
typedef enum {
  ScriptExprType_Value,
  ScriptExprType_Compare,

  ScriptExprType_Count,
} ScriptExprType;

/**
 * Handle to a Script expression.
 */
typedef u32 ScriptExpr;

/**
 * Create a new Script document.
 *
 * Should be destroyed using 'script_destroy()'.
 */
ScriptDoc* script_create(Allocator*);

/**
 * Destroy a Script document.
 */
void script_destroy(ScriptDoc*);

/**
 * Add new expressions.
 */
ScriptExpr script_add_value(ScriptDoc*, ScriptVal);
ScriptExpr script_add_compare(ScriptDoc*, ScriptExpr lhs, ScriptExpr rhs, ScriptComparison);

/**
 * Query expression data.
 */
ScriptExprType script_expr_type(const ScriptDoc*, ScriptExpr);

/**
 * Create a textual representation of the given expression.
 */
void   script_expr_str_write(const ScriptDoc*, ScriptExpr, u32 indent, DynString*);
String script_expr_str_scratch(const ScriptDoc*, ScriptExpr);

/**
 * Create a formatting argument for a expression.
 */
#define script_expr_fmt(_DOC_, _EXPR_) fmt_text(script_expr_str_scratch((_DOC_), (_EXPR_)))
