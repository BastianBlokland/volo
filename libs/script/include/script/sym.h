#pragma once
#include "script/forward.h"
#include "script/pos.h"

#define script_syms_max 4096
#define script_sym_sentinel sentinel_u16

typedef u16 ScriptSym;

typedef enum eScriptSymKind {
  ScriptSymKind_Keyword,
  ScriptSymKind_BuiltinConstant,
  ScriptSymKind_BuiltinFunction,
  ScriptSymKind_ExternFunction,
  ScriptSymKind_Variable,
  ScriptSymKind_MemoryKey,

  ScriptSymKind_Count,
} ScriptSymKind;

typedef u16 ScriptSymMask;
ASSERT(ScriptSymKind_Count < 16, "ScriptSymKind's have to be indexable with 16 bits");

#define script_sym_mask(_KIND_) ((ScriptSymMask)(1 << _KIND_))
#define script_sym_mask_none ((ScriptSymMask)0)
#define script_sym_mask_any ((ScriptSymMask)bit_range_32(0, ScriptSymKind_Count))
#define script_sym_mask_mem_key script_sym_mask(ScriptSymKind_MemoryKey)

typedef enum eScriptSymRefKind {
  ScriptSymRefKind_Read,
  ScriptSymRefKind_Write,
  ScriptSymRefKind_Call,
} ScriptSymRefKind;

typedef struct sScriptSymRef {
  ScriptSym        sym;
  ScriptSymRefKind kind : 16;
  ScriptRange      location;
} ScriptSymRef;

typedef struct sScriptSymBag ScriptSymBag;

ScriptSymBag* script_sym_bag_create(Allocator*, ScriptSymMask);
void          script_sym_bag_destroy(ScriptSymBag*);
void          script_sym_bag_clear(ScriptSymBag*);

// clang-format off

ScriptSym script_sym_push_keyword(ScriptSymBag*, String label);
ScriptSym script_sym_push_builtin_const(ScriptSymBag*, String label, ScriptVal);
ScriptSym script_sym_push_builtin_func(ScriptSymBag*, String label, String doc, ScriptIntrinsic, const ScriptSig*);
ScriptSym script_sym_push_extern_func(ScriptSymBag*, String label, String doc, ScriptBinderSlot, const ScriptSig*);
ScriptSym script_sym_push_var(ScriptSymBag*, String label, ScriptVarId, ScriptScopeId, ScriptRange location);
ScriptSym script_sym_push_mem_key(ScriptSymBag*, String label, StringHash key);

void script_sym_push_ref(ScriptSymBag*, ScriptSym, ScriptSymRefKind, ScriptRange location);

void script_sym_set_valid_range(ScriptSymBag*, ScriptSym, ScriptRange);

// clang-format on

ScriptSymKind    script_sym_kind(const ScriptSymBag*, ScriptSym);
String           script_sym_label(const ScriptSymBag*, ScriptSym);
String           script_sym_doc(const ScriptSymBag*, ScriptSym);
bool             script_sym_is_func(const ScriptSymBag*, ScriptSym);
ScriptRange      script_sym_location(const ScriptSymBag*, ScriptSym);
const ScriptSig* script_sym_sig(const ScriptSymBag*, ScriptSym);

ScriptSym script_sym_find(const ScriptSymBag*, const ScriptDoc*, ScriptExpr);

ScriptSym script_sym_first(const ScriptSymBag*, ScriptPos);
ScriptSym script_sym_next(const ScriptSymBag*, ScriptPos, ScriptSym);

typedef struct {
  const ScriptSymRef* begin;
  const ScriptSymRef* end;
} ScriptSymRefSet;

ScriptSymRefSet script_sym_refs(const ScriptSymBag*, ScriptSym);

String script_sym_kind_str(ScriptSymKind);
void   script_sym_write(DynString*, const ScriptSymBag*, ScriptSym);
String script_sym_scratch(const ScriptSymBag*, ScriptSym);
