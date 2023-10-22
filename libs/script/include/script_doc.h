#pragma once
#include "core_dynstring.h"
#include "core_types.h"
#include "script_intrinsic.h"
#include "script_val.h"

// Forward declare from 'core_alloc.h'.
typedef struct sAllocator Allocator;

// Forward declare from 'script_pos.h'.
typedef struct sScriptRange ScriptRange;

// Forward declare from 'script_binder.h'.
typedef u32 ScriptBinderSlot;

#define script_var_count 16
#define script_expr_sentinel sentinel_u32

typedef u8 ScriptVarId;

/**
 * Definition of a Script Document for storing script expressions.
 */
typedef struct sScriptDoc ScriptDoc;

/**
 * Type of a Script expression.
 */
typedef enum {
  ScriptExprType_Value,
  ScriptExprType_VarLoad,
  ScriptExprType_VarStore,
  ScriptExprType_MemLoad,
  ScriptExprType_MemStore,
  ScriptExprType_Intrinsic,
  ScriptExprType_Block,
  ScriptExprType_Extern,

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
 * Clear a Script document.
 * NOTE: After clearing all previously added expressions are invalided.
 */
void script_clear(ScriptDoc*);

// clang-format off

/**
 * Add new expressions.
 */
ScriptExpr script_add_value(ScriptDoc*, ScriptVal val);
ScriptExpr script_add_var_load(ScriptDoc*, ScriptVarId);
ScriptExpr script_add_var_store(ScriptDoc*, ScriptVarId, ScriptExpr val);
ScriptExpr script_add_mem_load(ScriptDoc*, StringHash key);
ScriptExpr script_add_mem_store(ScriptDoc*, StringHash key, ScriptExpr val);
ScriptExpr script_add_intrinsic(ScriptDoc*, ScriptIntrinsic, const ScriptExpr args[]);
ScriptExpr script_add_block(ScriptDoc*, const ScriptExpr exprs[], u32 exprCount);
ScriptExpr script_add_extern(ScriptDoc*, ScriptRange, ScriptBinderSlot, const ScriptExpr args[], u32 argCount);

// clang-format on

/**
 * Query expression data.
 */
ScriptExprType script_expr_type(const ScriptDoc*, ScriptExpr);
ScriptRange    script_expr_range(const ScriptDoc*, ScriptExpr);
bool           script_expr_readonly(const ScriptDoc*, ScriptExpr);
bool           script_expr_static(const ScriptDoc*, ScriptExpr);
bool           script_expr_always_truthy(const ScriptDoc*, ScriptExpr);
u32            script_values_total(const ScriptDoc*);

typedef void (*ScriptVisitor)(void* ctx, const ScriptDoc*, ScriptExpr);
void script_expr_visit(const ScriptDoc*, ScriptExpr, void* ctx, ScriptVisitor);

typedef enum {
  ScriptDocSignal_None     = 0,
  ScriptDocSignal_Continue = 1 << 0,
  ScriptDocSignal_Break    = 1 << 1,
  ScriptDocSignal_Return   = 1 << 2,
} ScriptDocSignal;

/**
 * Determine if the expression always returns an uncaught signal.
 * NOTE: Is conservative, code paths that depend on runtime values are not considered.
 */
ScriptDocSignal script_expr_always_uncaught_signal(const ScriptDoc*, ScriptExpr);

/**
 * Create a textual representation of the given expression.
 */
void   script_expr_str_write(const ScriptDoc*, ScriptExpr, u32 indent, DynString*);
String script_expr_str_scratch(const ScriptDoc*, ScriptExpr);

/**
 * Create a formatting argument for a expression.
 */
#define script_expr_fmt(_DOC_, _EXPR_) fmt_text(script_expr_str_scratch((_DOC_), (_EXPR_)))
