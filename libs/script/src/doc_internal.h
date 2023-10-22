#pragma once
#include "script_doc.h"

// Forward declare from 'script_binder.h'.
typedef u64 ScriptBinderSignature;

typedef u32 ScriptValId;
typedef u32 ScriptExprSet;

typedef struct {
  ScriptValId valId;
} ScriptExprValue;

typedef struct {
  ScriptVarId var;
} ScriptExprVarLoad;

typedef struct {
  ScriptVarId var;
  ScriptExpr  val;
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
  ScriptExprSet    argSet;
  u32              argCount;
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
  DynArray              exprData;   // ScriptExprData[]
  DynArray              exprTypes;  // u8[] (ScriptExprType[])
  DynArray              exprRanges; // ScriptRange[]
  DynArray              exprSets;   // ScriptExpr[]
  DynArray              values;     // ScriptVal[]
  Allocator*            alloc;
  ScriptBinderSignature binderSignature;
};
