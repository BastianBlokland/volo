#pragma once
#include "script_doc.h"

#define script_constants_max 15

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
  ScriptExpr    arg1;
  ScriptOpUnary op;
} ScriptExprOpUnary;

typedef struct {
  ScriptExpr     arg1;
  ScriptExpr     arg2;
  ScriptOpBinary op;
} ScriptExprOpBinary;

typedef struct {
  ScriptExpr      arg1;
  ScriptExpr      arg2;
  ScriptExpr      arg3;
  ScriptOpTernary op;
} ScriptExprOpTernary;

typedef struct {
  ScriptExprType type;
  union {
    ScriptExprValue     data_value;
    ScriptExprLoad      data_load;
    ScriptExprStore     data_store;
    ScriptExprOpUnary   data_op_unary;
    ScriptExprOpBinary  data_op_binary;
    ScriptExprOpTernary data_op_ternary;
  };
} ScriptExprData;

typedef struct {
  StringHash  nameHash;
  ScriptValId valId;
} ScriptConstant;

struct sScriptDoc {
  DynArray       exprs;  // ScriptExprData[]
  DynArray       values; // ScriptVal[]
  ScriptConstant constants[script_constants_max];
  Allocator*     alloc;
};

/**
 * Add new expressions.
 */
ScriptExpr script_add_value_id(ScriptDoc*, ScriptValId);

/**
 * Lookup a constant by name.
 * NOTE: Returns 'sentinel_u32' if no constant was found with the given name.
 * Pre-condition: namehash != 0.
 */
ScriptValId script_doc_constant_lookup(const ScriptDoc*, StringHash nameHash);
