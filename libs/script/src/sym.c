#include "core_alloc.h"
#include "core_array.h"
#include "core_diag.h"
#include "core_dynarray.h"
#include "core_search.h"
#include "script_intrinsic.h"
#include "script_sig.h"
#include "script_sym.h"
#include "script_val.h"

#include "doc_internal.h"

#define sym_transient_chunk_size (16 * usize_kibibyte)

ASSERT(script_syms_max < u16_max, "ScriptSym has to be storable as a 16-bit integer");

typedef struct {
  ScriptVal value;
} ScriptSymBuiltinConst;

typedef struct {
  ScriptIntrinsic intr;
  ScriptSig*      sig;
} ScriptSymBuiltinFunc;

typedef struct {
  ScriptBinderSlot binderSlot;
  ScriptSig*       sig;
} ScriptSymExternFunc;

typedef struct {
  ScriptVarId   slot; // NOTE: Only unique within the scope.
  ScriptScopeId scope;
  ScriptRange   location;
} ScriptSymVar;

typedef struct {
  StringHash key;
} ScriptSymMemKey;

typedef struct {
  ScriptSymKind kind;
  String        label;
  String        doc;
  ScriptRange   validRange;
  union {
    ScriptSymBuiltinConst builtinConst;
    ScriptSymBuiltinFunc  builtinFunc;
    ScriptSymExternFunc   externFunc;
    ScriptSymVar          var;
    ScriptSymMemKey       memKey;
  } data;
} ScriptSymData;

struct sScriptSymBag {
  Allocator* alloc;
  Allocator* allocTransient;
  DynArray   symbols;    // ScriptSym[]
  DynArray   references; // ScriptSymRef[], kept sorted on 'sym'.
};

static i8 sym_compare_ref(const void* a, const void* b) {
  const ScriptSymRef* entryA = a;
  const ScriptSymRef* entryB = b;
  return compare_u16(&entryA->sym, &entryB->sym);
}

static ScriptSym sym_push(ScriptSymBag* bag, ScriptSymData* data) {
  const ScriptSym id = (ScriptSym)bag->symbols.size;
  if (UNLIKELY(id == script_syms_max)) {
    return script_sym_sentinel;
  }
  *dynarray_push_t(&bag->symbols, ScriptSymData) = *data;
  return id;
}

static void sym_push_ref(ScriptSymBag* bag, ScriptSymRef* data) {
  *dynarray_insert_sorted_t(&bag->references, ScriptSymRef, sym_compare_ref, data) = *data;
}

INLINE_HINT static const ScriptSymData* sym_data(const ScriptSymBag* bag, const ScriptSym id) {
  diag_assert(id < bag->symbols.size);
  return &dynarray_begin_t(&bag->symbols, ScriptSymData)[id];
}

INLINE_HINT static ScriptSymData* sym_data_mut(ScriptSymBag* bag, const ScriptSym id) {
  diag_assert(id < bag->symbols.size);
  return &dynarray_begin_t(&bag->symbols, ScriptSymData)[id];
}

INLINE_HINT static bool sym_in_valid_range(const ScriptSymData* sym, const ScriptPos pos) {
  if (sentinel_check(pos)) {
    return true; // 'script_pos_sentinel' indicates that all ranges should be included.
  }
  if (sentinel_check(sym->validRange.start) || sentinel_check(sym->validRange.end)) {
    return true; // Symbol is valid in the entire document.
  }
  return script_range_contains(sym->validRange, pos);
}

static ScriptSym sym_find_value(const ScriptSymBag* b, const ScriptVal v) {
  for (ScriptSym id = 0; id != b->symbols.size; ++id) {
    const ScriptSymData* sym = sym_data(b, id);
    switch (sym->kind) {
    case ScriptSymKind_BuiltinConstant:
      if (script_val_equal(sym->data.builtinConst.value, v)) {
        return id;
      }
      break;
    default:
      break;
    }
  }
  return script_sym_sentinel;
}

static ScriptSym sym_find_intr(const ScriptSymBag* b, const ScriptIntrinsic intr) {
  for (ScriptSym id = 0; id != b->symbols.size; ++id) {
    const ScriptSymData* sym = sym_data(b, id);
    switch (sym->kind) {
    case ScriptSymKind_BuiltinFunction:
      if (sym->data.builtinFunc.intr == intr) {
        return id;
      }
      break;
    default:
      break;
    }
  }
  return script_sym_sentinel;
}

static ScriptSym sym_find_binder_slot(const ScriptSymBag* b, const ScriptBinderSlot slot) {
  for (ScriptSym id = 0; id != b->symbols.size; ++id) {
    const ScriptSymData* sym = sym_data(b, id);
    switch (sym->kind) {
    case ScriptSymKind_ExternFunction:
      if (sym->data.externFunc.binderSlot == slot) {
        return id;
      }
      break;
    default:
      break;
    }
  }
  return script_sym_sentinel;
}

static ScriptSym sym_find_var(const ScriptSymBag* b, const ScriptVarId v, const ScriptScopeId s) {
  for (ScriptSym id = 0; id != b->symbols.size; ++id) {
    const ScriptSymData* sym = sym_data(b, id);
    switch (sym->kind) {
    case ScriptSymKind_Variable:
      if (sym->data.var.slot == v && sym->data.var.scope == s) {
        return id;
      }
      break;
    default:
      break;
    }
  }
  return script_sym_sentinel;
}

static ScriptSym sym_find_mem_key(const ScriptSymBag* b, const StringHash memKey) {
  for (ScriptSym id = 0; id != b->symbols.size; ++id) {
    const ScriptSymData* sym = sym_data(b, id);
    switch (sym->kind) {
    case ScriptSymKind_MemoryKey:
      if (sym->data.memKey.key == memKey) {
        return id;
      }
      break;
    default:
      break;
    }
  }
  return script_sym_sentinel;
}

ScriptSymBag* script_sym_bag_create(Allocator* alloc) {
  ScriptSymBag* bag = alloc_alloc_t(alloc, ScriptSymBag);

  *bag = (ScriptSymBag){
      .alloc          = alloc,
      .allocTransient = alloc_chunked_create(alloc, alloc_bump_create, sym_transient_chunk_size),
      .symbols        = dynarray_create_t(alloc, ScriptSymData, 128),
      .references     = dynarray_create_t(alloc, ScriptSymRef, 128),
  };

  return bag;
}

void script_sym_bag_destroy(ScriptSymBag* bag) {
  dynarray_destroy(&bag->symbols);
  dynarray_destroy(&bag->references);
  alloc_chunked_destroy(bag->allocTransient);
  alloc_free_t(bag->alloc, bag);
}

void script_sym_bag_clear(ScriptSymBag* bag) {
  dynarray_clear(&bag->symbols);
  dynarray_clear(&bag->references);
  alloc_reset(bag->allocTransient);
}

ScriptSym script_sym_push_keyword(ScriptSymBag* bag, const String label) {
  diag_assert(!string_is_empty(label));

  // TODO: Report error when the transient allocator runs out of space.
  return sym_push(
      bag,
      &(ScriptSymData){
          .kind       = ScriptSymKind_Keyword,
          .label      = string_dup(bag->allocTransient, label),
          .validRange = script_range_sentinel,
      });
}

ScriptSym script_sym_push_builtin_const(ScriptSymBag* bag, const String label, const ScriptVal v) {
  diag_assert(!string_is_empty(label));

  // TODO: Report error when the transient allocator runs out of space.
  return sym_push(
      bag,
      &(ScriptSymData){
          .kind                    = ScriptSymKind_BuiltinConstant,
          .label                   = string_dup(bag->allocTransient, label),
          .validRange              = script_range_sentinel,
          .data.builtinConst.value = v,
      });
}

ScriptSym script_sym_push_builtin_func(
    ScriptSymBag*         bag,
    const String          label,
    const String          doc,
    const ScriptIntrinsic intr,
    const ScriptSig*      sig) {
  diag_assert(!string_is_empty(label));

  // TODO: Report error when the transient allocator runs out of space.
  return sym_push(
      bag,
      &(ScriptSymData){
          .kind                  = ScriptSymKind_BuiltinFunction,
          .label                 = string_dup(bag->allocTransient, label),
          .doc                   = string_maybe_dup(bag->allocTransient, doc),
          .validRange            = script_range_sentinel,
          .data.builtinFunc.intr = intr,
          .data.builtinFunc.sig  = sig ? script_sig_clone(bag->allocTransient, sig) : null,
      });
}

ScriptSym script_sym_push_extern_func(
    ScriptSymBag*          bag,
    const String           label,
    const String           doc,
    const ScriptBinderSlot binderSlot,
    const ScriptSig*       sig) {
  diag_assert(!string_is_empty(label));

  // TODO: Report error when the transient allocator runs out of space.
  return sym_push(
      bag,
      &(ScriptSymData){
          .kind                       = ScriptSymKind_ExternFunction,
          .label                      = string_dup(bag->allocTransient, label),
          .doc                        = string_maybe_dup(bag->allocTransient, doc),
          .validRange                 = script_range_sentinel,
          .data.externFunc.binderSlot = binderSlot,
          .data.externFunc.sig        = sig ? script_sig_clone(bag->allocTransient, sig) : null,
      });
}

ScriptSym script_sym_push_var(
    ScriptSymBag*       bag,
    const String        label,
    const ScriptVarId   slot,
    const ScriptScopeId scope,
    const ScriptRange   location) {
  diag_assert(!string_is_empty(label));

  // TODO: Report error when the transient allocator runs out of space.
  return sym_push(
      bag,
      &(ScriptSymData){
          .kind              = ScriptSymKind_Variable,
          .label             = string_dup(bag->allocTransient, label),
          .validRange        = script_range_sentinel,
          .data.var.slot     = slot,
          .data.var.scope    = scope,
          .data.var.location = location,
      });
}

ScriptSym script_sym_push_mem_key(ScriptSymBag* bag, const String label, const StringHash key) {
  diag_assert(!string_is_empty(label));

  // TODO: Report error when the transient allocator runs out of space.
  return sym_push(
      bag,
      &(ScriptSymData){
          .kind            = ScriptSymKind_MemoryKey,
          .label           = string_dup(bag->allocTransient, label),
          .validRange      = script_range_sentinel,
          .data.memKey.key = key,
      });
}

void script_sym_push_ref(
    ScriptSymBag*          bag,
    const ScriptSym        sym,
    const ScriptSymRefKind kind,
    const ScriptRange      location) {
  diag_assert(sym < bag->symbols.size);
  sym_push_ref(
      bag,
      &(ScriptSymRef){
          .sym      = sym,
          .kind     = kind,
          .location = location,
      });
}

void script_sym_set_valid_range(ScriptSymBag* bag, const ScriptSym sym, const ScriptRange range) {
  sym_data_mut(bag, sym)->validRange = range;
}

ScriptSymKind script_sym_kind(const ScriptSymBag* bag, const ScriptSym sym) {
  return sym_data(bag, sym)->kind;
}

String script_sym_label(const ScriptSymBag* bag, const ScriptSym sym) {
  return sym_data(bag, sym)->label;
}

String script_sym_doc(const ScriptSymBag* bag, const ScriptSym sym) {
  return sym_data(bag, sym)->doc;
}

bool script_sym_is_func(const ScriptSymBag* bag, const ScriptSym sym) {
  const ScriptSymData* symData = sym_data(bag, sym);
  return symData->kind == ScriptSymKind_BuiltinFunction ||
         symData->kind == ScriptSymKind_ExternFunction;
}

ScriptRange script_sym_location(const ScriptSymBag* bag, const ScriptSym sym) {
  const ScriptSymData* symData = sym_data(bag, sym);
  switch (symData->kind) {
  case ScriptSymKind_Variable:
    return symData->data.var.location;
  default:
    break;
  }
  return script_range_sentinel;
}

const ScriptSig* script_sym_sig(const ScriptSymBag* bag, const ScriptSym sym) {
  const ScriptSymData* symData = sym_data(bag, sym);
  switch (symData->kind) {
  case ScriptSymKind_BuiltinFunction:
    return symData->data.builtinFunc.sig;
  case ScriptSymKind_ExternFunction:
    return symData->data.externFunc.sig;
  default:
    break;
  }
  return null;
}

ScriptSym script_sym_find(const ScriptSymBag* bag, const ScriptDoc* doc, const ScriptExpr expr) {
  const ScriptExprData* exprData = expr_data(doc, expr);
  switch (expr_kind(doc, expr)) {
  case ScriptExprKind_Value:
    return sym_find_value(bag, dynarray_begin_t(&doc->values, ScriptVal)[exprData->value.valId]);
  case ScriptExprKind_Intrinsic:
    return sym_find_intr(bag, exprData->intrinsic.intrinsic);
  case ScriptExprKind_VarLoad:
    return sym_find_var(bag, exprData->var_load.var, exprData->var_load.scope);
  case ScriptExprKind_VarStore:
    return sym_find_var(bag, exprData->var_store.var, exprData->var_load.scope);
  case ScriptExprKind_MemLoad:
    return sym_find_mem_key(bag, exprData->mem_load.key);
  case ScriptExprKind_MemStore:
    return sym_find_mem_key(bag, exprData->mem_store.key);
  case ScriptExprKind_Extern:
    return sym_find_binder_slot(bag, exprData->extern_.func);
  default:
    return script_sym_sentinel;
  }
}

ScriptSym script_sym_first(const ScriptSymBag* bag, const ScriptPos pos) {
  if (!bag->symbols.size) {
    return script_sym_sentinel;
  }
  const ScriptSymData* first = sym_data(bag, 0);
  return sym_in_valid_range(first, pos) ? 0 : script_sym_next(bag, 0, pos);
}

ScriptSym script_sym_next(const ScriptSymBag* bag, const ScriptPos pos, ScriptSym itr) {
  const ScriptSym lastId = (ScriptSym)(bag->symbols.size - 1);
  while (itr < lastId) {
    if (sym_in_valid_range(sym_data(bag, ++itr), pos)) {
      return itr;
    }
  }
  return script_sym_sentinel;
}

ScriptSymRefSet script_sym_refs(const ScriptSymBag* bag, const ScriptSym sym) {
  const ScriptSymRef* begin = dynarray_begin_t(&bag->references, ScriptSymRef);
  const ScriptSymRef* end   = dynarray_end_t(&bag->references, ScriptSymRef);
  const ScriptSymRef  tgt   = {.sym = sym};

  const ScriptSymRef* res = search_binary_t(begin, end, ScriptSymRef, sym_compare_ref, &tgt);
  if (!res) {
    // No references found for the given symbol.
    return (ScriptSymRefSet){null, null};
  }

  // Find the beginning of the references for this symbol.
  while (res != begin && (res - 1)->sym == sym) {
    --res;
  }

  // Find the end of the references for this symbol.
  const ScriptSymRef* resEnd = res;
  while (resEnd != end && (resEnd + 1)->sym == sym) {
    ++resEnd;
  }

  return (ScriptSymRefSet){.begin = res, .end = resEnd + 1};
}

String script_sym_kind_str(const ScriptSymKind kind) {
  static const String g_names[] = {
      string_static("Keyword"),
      string_static("BuiltinConstant"),
      string_static("BuiltinFunction"),
      string_static("ExternFunction"),
      string_static("Variable"),
      string_static("MemoryKey"),
  };
  ASSERT(array_elems(g_names) == ScriptSymKind_Count, "Incorrect number of ScriptSymKind names");

  diag_assert(kind < ScriptSymKind_Count);
  return g_names[kind];
}

void script_sym_write(DynString* out, const ScriptSymBag* bag, const ScriptSym sym) {
  const ScriptSymData* symData = sym_data(bag, sym);
  fmt_write(out, "[{}] {}", fmt_text(script_sym_kind_str(symData->kind)), fmt_text(symData->label));
}

String script_sym_scratch(const ScriptSymBag* bag, const ScriptSym sym) {
  Mem       bufferMem = alloc_alloc(g_allocScratch, usize_kibibyte, 1);
  DynString buffer    = dynstring_create_over(bufferMem);

  script_sym_write(&buffer, bag, sym);

  return dynstring_view(&buffer);
}
