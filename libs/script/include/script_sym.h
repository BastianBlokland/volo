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
typedef u32               ScriptScopeId;

// Forward declare from 'script_sig.h'.
typedef struct sScriptSig ScriptSig;

// Forward declare from 'script_binder.h'.
typedef u16 ScriptBinderSlot;

#define script_syms_max 4096
#define script_sym_sentinel sentinel_u16

typedef u16 ScriptSym;

typedef enum {
  ScriptSymKind_Keyword,
  ScriptSymKind_BuiltinConstant,
  ScriptSymKind_BuiltinFunction,
  ScriptSymKind_ExternFunction,
  ScriptSymKind_Variable,
  ScriptSymKind_MemoryKey,

  ScriptSymKind_Count,
} ScriptSymKind;

typedef struct sScriptSymBag ScriptSymBag;

ScriptSymBag* script_sym_bag_create(Allocator*);
void          script_sym_bag_destroy(ScriptSymBag*);
void          script_sym_bag_clear(ScriptSymBag*);

// clang-format off

ScriptSym script_sym_push_keyword(ScriptSymBag*, String label);
ScriptSym script_sym_push_builtin_const(ScriptSymBag*, String label);
ScriptSym script_sym_push_builtin_func(ScriptSymBag*, String label, String doc, ScriptIntrinsic, const ScriptSig*);
ScriptSym script_sym_push_extern_func(ScriptSymBag*, String label, String doc, ScriptBinderSlot, const ScriptSig*);
ScriptSym script_sym_push_var(ScriptSymBag*, String label, ScriptVarId, ScriptScopeId, ScriptRange location);
ScriptSym script_sym_push_mem_key(ScriptSymBag*, String label, StringHash key);

void script_sym_push_ref(ScriptSymBag*, ScriptSym, ScriptRange location);

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

String script_sym_kind_str(ScriptSymKind);
void   script_sym_write(DynString*, const ScriptSymBag*, ScriptSym);
String script_sym_scratch(const ScriptSymBag*, ScriptSym);
