#pragma once
#include "core_dynstring.h"
#include "core_types.h"
#include "script_intrinsic.h"
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
  ScriptExprType_MemLoad,
  ScriptExprType_MemStore,
  ScriptExprType_Intrinsic,
  ScriptExprType_Block,

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
ScriptExpr script_add_value(ScriptDoc*, ScriptVal val);
ScriptExpr script_add_mem_load(ScriptDoc*, StringHash key);
ScriptExpr script_add_mem_store(ScriptDoc*, StringHash key, ScriptExpr val);
ScriptExpr script_add_intrinsic(ScriptDoc*, ScriptIntrinsic, const ScriptExpr args[]);
ScriptExpr script_add_block(ScriptDoc*, const ScriptExpr exprs[], u32 exprCount);

/**
 * Query expression data.
 */
ScriptExprType script_expr_type(const ScriptDoc*, ScriptExpr);
bool           script_expr_readonly(const ScriptDoc*, ScriptExpr);

typedef void (*ScriptVisitor)(void* ctx, const ScriptDoc*, ScriptExpr);
void script_expr_visit(const ScriptDoc*, ScriptExpr, void* ctx, ScriptVisitor);

/**
 * Create a textual representation of the given expression.
 */
void   script_expr_str_write(const ScriptDoc*, ScriptExpr, u32 indent, DynString*);
String script_expr_str_scratch(const ScriptDoc*, ScriptExpr);

/**
 * Create a formatting argument for a expression.
 */
#define script_expr_fmt(_DOC_, _EXPR_) fmt_text(script_expr_str_scratch((_DOC_), (_EXPR_)))
