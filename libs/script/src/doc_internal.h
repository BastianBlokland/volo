#pragma once
#include "script_doc.h"

typedef u32 ScriptValId;
typedef u32 ScriptExprSet;

typedef struct {
  ScriptValId valId;
} ScriptExprValue;

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
  ScriptExprType type;
  union {
    ScriptExprValue     data_value;
    ScriptExprMemLoad   data_mem_load;
    ScriptExprMemStore  data_mem_store;
    ScriptExprIntrinsic data_intrinsic;
    ScriptExprBlock     data_block;
  };
} ScriptExprData;

struct sScriptDoc {
  DynArray   exprData; // ScriptExprData[]
  DynArray   exprSets; // ScriptExpr[]
  DynArray   values;   // ScriptVal[]
  Allocator* alloc;
};
