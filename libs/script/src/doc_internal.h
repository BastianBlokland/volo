#pragma once
#include "script_doc.h"

#define script_constants_max 15

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
  ScriptOpNullary op;
} ScriptExprOpNullary;

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
  ScriptExprSet exprSet;
  u32           exprCount;
} ScriptExprBlock;

typedef struct {
  ScriptExprType type;
  union {
    ScriptExprValue     data_value;
    ScriptExprMemLoad   data_mem_load;
    ScriptExprMemStore  data_mem_store;
    ScriptExprOpNullary data_op_nullary;
    ScriptExprOpUnary   data_op_unary;
    ScriptExprOpBinary  data_op_binary;
    ScriptExprOpTernary data_op_ternary;
    ScriptExprBlock     data_block;
  };
} ScriptExprData;

typedef struct {
  StringHash  nameHash;
  ScriptValId valId;
} ScriptConstant;

struct sScriptDoc {
  DynArray       exprData; // ScriptExprData[]
  DynArray       exprSets; // ScriptExpr[]
  DynArray       values;   // ScriptVal[]
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
