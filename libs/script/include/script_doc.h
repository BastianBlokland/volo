#pragma once
#include "script.h"

#define script_var_count 16
#define script_var_sentinel sentinel_u8
#define script_scope_sentinel sentinel_u32
#define script_expr_sentinel sentinel_u32

typedef u8  ScriptVarId;
typedef u32 ScriptScopeId;

/**
 * Definition of a Script Document for storing script expressions.
 */
typedef struct sScriptDoc ScriptDoc;

typedef enum eScriptExprKind {
  ScriptExprKind_Value,
  ScriptExprKind_VarLoad,
  ScriptExprKind_VarStore,
  ScriptExprKind_MemLoad,
  ScriptExprKind_MemStore,
  ScriptExprKind_Intrinsic,
  ScriptExprKind_Block,
  ScriptExprKind_Extern,

  ScriptExprKind_Count,
} ScriptExprKind;

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
ScriptExpr script_add_value(ScriptDoc*, ScriptRange, ScriptVal val);
ScriptExpr script_add_var_load(ScriptDoc*, ScriptRange, ScriptScopeId, ScriptVarId);
ScriptExpr script_add_var_store(ScriptDoc*, ScriptRange, ScriptScopeId, ScriptVarId, ScriptExpr val);
ScriptExpr script_add_mem_load(ScriptDoc*, ScriptRange, StringHash key);
ScriptExpr script_add_mem_store(ScriptDoc*, ScriptRange, StringHash key, ScriptExpr val);
ScriptExpr script_add_intrinsic(ScriptDoc*, ScriptRange, ScriptIntrinsic, const ScriptExpr args[]);
ScriptExpr script_add_block(ScriptDoc*, ScriptRange, const ScriptExpr exprs[], u32 exprCount);
ScriptExpr script_add_extern(ScriptDoc*, ScriptRange, ScriptBinderSlot, const ScriptExpr args[], u16 argCount);

ScriptExpr script_add_anon_value(ScriptDoc*, ScriptVal val);
ScriptExpr script_add_anon_var_load(ScriptDoc*, ScriptScopeId, ScriptVarId);
ScriptExpr script_add_anon_var_store(ScriptDoc*, ScriptScopeId, ScriptVarId, ScriptExpr val);
ScriptExpr script_add_anon_mem_load(ScriptDoc*, StringHash key);
ScriptExpr script_add_anon_mem_store(ScriptDoc*, StringHash key, ScriptExpr val);
ScriptExpr script_add_anon_intrinsic(ScriptDoc*, ScriptIntrinsic, const ScriptExpr args[]);

// clang-format on

/**
 * Query expression data.
 */
u32            script_values_total(const ScriptDoc*);
ScriptExprKind script_expr_kind(const ScriptDoc*, ScriptExpr);
ScriptRange    script_expr_range(const ScriptDoc*, ScriptExpr);
bool           script_expr_static(const ScriptDoc*, ScriptExpr);
ScriptVal      script_expr_static_val(const ScriptDoc*, ScriptExpr);
bool           script_expr_always_truthy(const ScriptDoc*, ScriptExpr);
bool           script_expr_is_intrinsic(const ScriptDoc*, ScriptExpr, ScriptIntrinsic);

typedef bool (*ScriptPred)(void* ctx, const ScriptDoc*, ScriptExpr);
ScriptExpr script_expr_find(const ScriptDoc*, ScriptExpr root, ScriptPos, void* ctx, ScriptPred);

u32 script_expr_arg_count(const ScriptDoc*, ScriptExpr);
u32 script_expr_arg_index(const ScriptDoc*, ScriptExpr, ScriptPos);

typedef void (*ScriptVisitor)(void* ctx, const ScriptDoc*, ScriptExpr);
void script_expr_visit(const ScriptDoc*, ScriptExpr, void* ctx, ScriptVisitor);

typedef ScriptExpr (*ScriptRewriter)(void* ctx, ScriptDoc*, ScriptExpr);
ScriptExpr script_expr_rewrite(ScriptDoc*, ScriptExpr, void* ctx, ScriptRewriter);

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
String script_expr_kind_str(ScriptExprKind);
void   script_expr_write(const ScriptDoc*, ScriptExpr, u32 indent, DynString*);
String script_expr_scratch(const ScriptDoc*, ScriptExpr);

/**
 * Create a formatting argument for a expression.
 */
#define script_expr_fmt(_DOC_, _EXPR_) fmt_text(script_expr_scratch((_DOC_), (_EXPR_)))
