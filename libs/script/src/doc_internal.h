#pragma once
#include "script_doc.h"
#include "script_pos.h"

// Forward declare from 'script_binder.h'.
typedef u64 ScriptBinderHash;

typedef u32 ScriptValId;
typedef u32 ScriptExprSet;

typedef struct {
  ScriptValId valId;
} ScriptExprValue;

typedef struct {
  ScriptScopeId scope;
  ScriptVarId   var;
} ScriptExprVarLoad;

typedef struct {
  ScriptScopeId scope;
  ScriptVarId   var;
  ScriptExpr    val;
} ScriptExprVarStore;

typedef struct {
  StringHash key;
} ScriptExprMemLoad;

typedef struct {
  StringHash key;
  ScriptExpr val;
} ScriptExprMemStore;

typedef struct {
  ScriptExprSet   argSet;
  ScriptIntrinsic intrinsic;
} ScriptExprIntrinsic;

typedef struct {
  ScriptExprSet exprSet;
  u32           exprCount;
} ScriptExprBlock;

typedef struct {
  ScriptBinderSlot func;
  u16              argCount;
  ScriptExprSet    argSet;
} ScriptExprExtern;

typedef union {
  ScriptExprValue     value;
  ScriptExprVarLoad   var_load;
  ScriptExprVarStore  var_store;
  ScriptExprMemLoad   mem_load;
  ScriptExprMemStore  mem_store;
  ScriptExprIntrinsic intrinsic;
  ScriptExprBlock     block;
  ScriptExprExtern    extern_;
} ScriptExprData;

struct sScriptDoc {
  DynArray         exprData;   // ScriptExprData[]
  DynArray         exprKinds;  // u8[] (ScriptExprKind[])
  DynArray         exprRanges; // ScriptRange[]
  DynArray         exprSets;   // ScriptExpr[]
  DynArray         values;     // ScriptVal[]
  Allocator*       alloc;
  ScriptBinderHash binderHash;
};

// clang-format off

MAYBE_UNUSED INLINE_HINT static ScriptExprKind expr_kind(const ScriptDoc* d, const ScriptExpr e) {
  return (ScriptExprKind)(dynarray_begin_t(&d->exprKinds, u8)[e]);
}

MAYBE_UNUSED INLINE_HINT static const ScriptExprData* expr_data(const ScriptDoc* d, const ScriptExpr e) {
  return &dynarray_begin_t(&d->exprData, ScriptExprData)[e];
}

MAYBE_UNUSED INLINE_HINT static ScriptRange expr_range(const ScriptDoc* d, const ScriptExpr e) {
  return dynarray_begin_t(&d->exprRanges, ScriptRange)[e];
}

INLINE_HINT MAYBE_UNUSED static const ScriptExpr* expr_set_data(const ScriptDoc* d, const ScriptExprSet s) {
  return &dynarray_begin_t(&d->exprSets, ScriptExpr)[s];
}

// clang-format on
