#pragma once
#include "script_doc.h"

#define script_constants_max 10

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
