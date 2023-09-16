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

typedef struct {
  ScriptExprType type;
  union {
    ScriptExprValue     data_value;
    ScriptExprVarLoad   data_var_load;
    ScriptExprVarStore  data_var_store;
    ScriptExprMemLoad   data_mem_load;
    ScriptExprMemStore  data_mem_store;
    ScriptExprIntrinsic data_intrinsic;
    ScriptExprBlock     data_block;
    ScriptExprExtern    data_extern;
  };
} ScriptExprData;

struct sScriptDoc {
  DynArray              exprData; // ScriptExprData[]
  DynArray              exprSets; // ScriptExpr[]
  DynArray              values;   // ScriptVal[]
  Allocator*            alloc;
  ScriptBinderSignature binderSignature;
};
