#pragma once
#include "script_intrinsic.h"
#include "script_pos.h"

// Forward declare from 'core_alloc.h'.
typedef struct sAllocator Allocator;

// Forward declare from 'core_dynstring.h'.
typedef struct sDynArray DynString;

// Forward declare from 'script_doc.h'.
typedef struct sScriptDoc ScriptDoc;
typedef u32               ScriptExpr;
typedef u8                ScriptVarId;

// Forward declare from 'script_sig.h'.
typedef struct sScriptSig ScriptSig;

// Forward declare from 'script_binder.h'.
typedef u16 ScriptBinderSlot;

#define script_syms_max 4096
#define script_sym_sentinel sentinel_u16

typedef u16 ScriptSym;

typedef enum {
  ScriptSymType_Keyword,
  ScriptSymType_BuiltinConstant,
  ScriptSymType_BuiltinFunction,
  ScriptSymType_ExternFunction,
  ScriptSymType_Variable,
  ScriptSymType_MemoryKey,

  ScriptSymType_Count,
} ScriptSymType;

typedef struct {
  ScriptIntrinsic intr;
  ScriptSig*      sig;
} ScriptSymBuiltinFunc;

typedef struct {
  ScriptBinderSlot binderSlot;
} ScriptSymExternFunc;

typedef struct {
  ScriptVarId slot; // NOTE: Only unique within the scope.
  ScriptRange location;
  ScriptRange scope;
} ScriptSymVar;

typedef struct {
  StringHash key;
} ScriptSymMemKey;

typedef struct {
  ScriptSymType type;
  String        label;
  String        doc;
  union {
    ScriptSymBuiltinFunc builtinFunc;
    ScriptSymExternFunc  externFunc;
    ScriptSymVar         var;
    ScriptSymMemKey      memKey;
  } data;
} ScriptSymData;

typedef struct sScriptSymBag ScriptSymBag;

ScriptSymBag* script_sym_bag_create(Allocator*);
void          script_sym_bag_destroy(ScriptSymBag*);
void          script_sym_bag_clear(ScriptSymBag*);

ScriptSym script_sym_push(ScriptSymBag*, const ScriptSymData*);

ScriptSymType    script_sym_type(const ScriptSymBag*, ScriptSym);
String           script_sym_label(const ScriptSymBag*, ScriptSym);
String           script_sym_doc(const ScriptSymBag*, ScriptSym);
bool             script_sym_is_func(const ScriptSymBag*, ScriptSym);
ScriptRange      script_sym_location(const ScriptSymBag*, ScriptSym);
const ScriptSig* script_sym_sig(const ScriptSymBag*, ScriptSym);
String           script_sym_type_str(ScriptSymType);

ScriptSym script_sym_find(const ScriptSymBag*, const ScriptDoc*, ScriptExpr);

ScriptSym script_sym_first(const ScriptSymBag*, ScriptPos);
ScriptSym script_sym_next(const ScriptSymBag*, ScriptPos, ScriptSym);

void   script_sym_write(DynString*, const ScriptSymBag*, ScriptSym);
String script_sym_scratch(const ScriptSymBag*, ScriptSym);
