#pragma once
#include "script_doc.h"

typedef u32 ScriptValId;

typedef struct {
  ScriptValId valId;
} ScriptExprValue;

typedef struct {
  StringHash key;
} ScriptExprLoad;

typedef struct {
  StringHash key;
  ScriptExpr val;
} ScriptExprStore;

typedef struct {
  ScriptExpr    val;
  ScriptOpUnary op;
} ScriptExprOpUnary;

typedef struct {
  ScriptExpr     lhs;
  ScriptExpr     rhs;
  ScriptOpBinary op;
} ScriptExprOpBinary;

typedef struct {
  ScriptExprType type;
  union {
    ScriptExprValue    data_value;
    ScriptExprLoad     data_load;
    ScriptExprStore    data_store;
    ScriptExprOpUnary  data_op_unary;
    ScriptExprOpBinary data_op_binary;
  };
} ScriptExprData;

struct sScriptDoc {
  DynArray   exprs;  // ScriptExprData[]
  DynArray   values; // ScriptVal[]
  Allocator* alloc;
};
