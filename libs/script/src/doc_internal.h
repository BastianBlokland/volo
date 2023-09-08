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
