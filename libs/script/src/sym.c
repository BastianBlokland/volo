#include "core_alloc.h"
#include "core_array.h"
#include "core_diag.h"
#include "core_dynarray.h"
#include "script_sig.h"
#include "script_sym.h"

#include "doc_internal.h"

ASSERT(script_syms_max < u16_max, "ScriptSym has to be storable as a 16-bit integer");

typedef struct {
  ScriptIntrinsic intr;
  ScriptSig*      sig;
} ScriptSymBuiltinFunc;

typedef struct {
  ScriptBinderSlot binderSlot;
  ScriptSig*       sig;
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
  ScriptSymKind kind;
  String        label;
  String        doc;
  union {
    ScriptSymBuiltinFunc builtinFunc;
    ScriptSymExternFunc  externFunc;
    ScriptSymVar         var;
    ScriptSymMemKey      memKey;
  } data;
} ScriptSymData;

struct sScriptSymBag {
  Allocator* alloc;
  DynArray   symbols; // ScriptSym[]
};

static ScriptSym sym_push(ScriptSymBag* bag, ScriptSymData* data) {
  const ScriptSym id = (ScriptSym)bag->symbols.size;
  if (UNLIKELY(id == script_syms_max)) {
    return script_sym_sentinel;
  }
  *dynarray_push_t(&bag->symbols, ScriptSymData) = *data;
  return id;
}

INLINE_HINT static const ScriptSymData* sym_data(const ScriptSymBag* bag, const ScriptSym id) {
  diag_assert(id < bag->symbols.size);
  return &dynarray_begin_t(&bag->symbols, ScriptSymData)[id];
}

INLINE_HINT static bool sym_in_scope(const ScriptSymData* sym, const ScriptPos pos) {
  switch (sym->kind) {
  case ScriptSymKind_Variable:
    if (sentinel_check(pos)) {
      return true; // 'script_pos_sentinel' indicates that all ranges should be included.
    }
    return script_range_contains(sym->data.var.scope, pos);
  default:
    return true;
  }
}

static ScriptSym sym_find_by_intr(const ScriptSymBag* b, const ScriptIntrinsic intr) {
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

static ScriptSym sym_find_by_binder_slot(const ScriptSymBag* b, const ScriptBinderSlot slot) {
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

static ScriptSym sym_find_by_var(const ScriptSymBag* b, const ScriptVarId v, const ScriptPos p) {
  for (ScriptSym id = 0; id != b->symbols.size; ++id) {
    const ScriptSymData* sym = sym_data(b, id);
    switch (sym->kind) {
    case ScriptSymKind_Variable:
      if (sym->data.var.slot == v && sym_in_scope(sym, p)) {
        return id;
      }
      break;
    default:
      break;
    }
  }
  return script_sym_sentinel;
}

static ScriptSym sym_find_by_mem_key(const ScriptSymBag* b, const StringHash memKey) {
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
      .alloc   = alloc,
      .symbols = dynarray_create_t(alloc, ScriptSymData, 128),
  };

  return bag;
}

void script_sym_bag_destroy(ScriptSymBag* bag) {
  script_sym_bag_clear(bag);
  dynarray_destroy(&bag->symbols);
  alloc_free_t(bag->alloc, bag);
}

void script_sym_bag_clear(ScriptSymBag* bag) {
  dynarray_for_t(&bag->symbols, ScriptSymData, sym) {
    string_free(bag->alloc, sym->label);
    string_maybe_free(bag->alloc, sym->doc);
    switch (sym->kind) {
    case ScriptSymKind_BuiltinFunction:
      if (sym->data.builtinFunc.sig) {
        script_sig_destroy(sym->data.builtinFunc.sig);
      }
      break;
    case ScriptSymKind_ExternFunction:
      if (sym->data.externFunc.sig) {
        script_sig_destroy(sym->data.externFunc.sig);
      }
      break;
    case ScriptSymKind_Variable:
    case ScriptSymKind_MemoryKey:
    case ScriptSymKind_BuiltinConstant:
    case ScriptSymKind_Keyword:
    case ScriptSymKind_Count:
      break;
    }
  }
  dynarray_clear(&bag->symbols);
}

ScriptSym script_sym_push_keyword(ScriptSymBag* bag, const String label) {
  diag_assert(!string_is_empty(label));

  return sym_push(
      bag,
      &(ScriptSymData){
          .kind  = ScriptSymKind_Keyword,
          .label = string_dup(bag->alloc, label),
      });
}

ScriptSym script_sym_push_builtin_const(ScriptSymBag* bag, const String label) {
  diag_assert(!string_is_empty(label));

  return sym_push(
      bag,
      &(ScriptSymData){
          .kind  = ScriptSymKind_BuiltinConstant,
          .label = string_dup(bag->alloc, label),
      });
}

ScriptSym script_sym_push_builtin_func(
    ScriptSymBag*         bag,
    const String          label,
    const String          doc,
    const ScriptIntrinsic intr,
    const ScriptSig*      sig) {
  diag_assert(!string_is_empty(label));

  return sym_push(
      bag,
      &(ScriptSymData){
          .kind                  = ScriptSymKind_BuiltinFunction,
          .label                 = string_dup(bag->alloc, label),
          .doc                   = string_maybe_dup(bag->alloc, doc),
          .data.builtinFunc.intr = intr,
          .data.builtinFunc.sig  = sig ? script_sig_clone(bag->alloc, sig) : null,
      });
}

ScriptSym script_sym_push_extern_func(
    ScriptSymBag*          bag,
    const String           label,
    const String           doc,
    const ScriptBinderSlot binderSlot,
    const ScriptSig*       sig) {
  diag_assert(!string_is_empty(label));

  return sym_push(
      bag,
      &(ScriptSymData){
          .kind                       = ScriptSymKind_ExternFunction,
          .label                      = string_dup(bag->alloc, label),
          .doc                        = string_maybe_dup(bag->alloc, doc),
          .data.externFunc.binderSlot = binderSlot,
          .data.externFunc.sig        = sig ? script_sig_clone(bag->alloc, sig) : null,
      });
}

ScriptSym script_sym_push_var(
    ScriptSymBag*     bag,
    const String      label,
    const ScriptVarId slot,
    const ScriptRange location,
    const ScriptRange scope) {
  diag_assert(!string_is_empty(label));

  return sym_push(
      bag,
      &(ScriptSymData){
          .kind              = ScriptSymKind_Variable,
          .label             = string_dup(bag->alloc, label),
          .data.var.slot     = slot,
          .data.var.location = location,
          .data.var.scope    = scope,
      });
}

ScriptSym script_sym_push_mem_key(ScriptSymBag* bag, const String label, const StringHash key) {
  diag_assert(!string_is_empty(label));

  return sym_push(
      bag,
      &(ScriptSymData){
          .kind            = ScriptSymKind_MemoryKey,
          .label           = string_dup(bag->alloc, label),
          .data.memKey.key = key,
      });
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
  switch (expr_kind(doc, expr)) {
  case ScriptExprKind_Intrinsic:
    return sym_find_by_intr(bag, expr_data(doc, expr)->intrinsic.intrinsic);
  case ScriptExprKind_VarLoad:
    return sym_find_by_var(bag, expr_data(doc, expr)->var_load.var, expr_range(doc, expr).start);
  case ScriptExprKind_VarStore:
    return sym_find_by_var(bag, expr_data(doc, expr)->var_store.var, expr_range(doc, expr).start);
  case ScriptExprKind_MemLoad:
    return sym_find_by_mem_key(bag, expr_data(doc, expr)->mem_load.key);
  case ScriptExprKind_MemStore:
    return sym_find_by_mem_key(bag, expr_data(doc, expr)->mem_store.key);
  case ScriptExprKind_Extern:
    return sym_find_by_binder_slot(bag, expr_data(doc, expr)->extern_.func);
  default:
    return script_sym_sentinel;
  }
}

ScriptSym script_sym_first(const ScriptSymBag* bag, const ScriptPos pos) {
  if (!bag->symbols.size) {
    return script_sym_sentinel;
  }
  const ScriptSymData* first = sym_data(bag, 0);
  return sym_in_scope(first, pos) ? 0 : script_sym_next(bag, 0, pos);
}

ScriptSym script_sym_next(const ScriptSymBag* bag, const ScriptPos pos, ScriptSym itr) {
  const ScriptSym lastId = (ScriptSym)(bag->symbols.size - 1);
  while (itr < lastId) {
    if (sym_in_scope(sym_data(bag, ++itr), pos)) {
      return itr;
    }
  }
  return script_sym_sentinel;
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
  Mem       bufferMem = alloc_alloc(g_alloc_scratch, usize_kibibyte, 1);
  DynString buffer    = dynstring_create_over(bufferMem);

  script_sym_write(&buffer, bag, sym);

  return dynstring_view(&buffer);
}
