#include "core_alloc.h"
#include "core_array.h"
#include "core_diag.h"
#include "core_dynarray.h"
#include "script_sig.h"
#include "script_sym.h"

#include "doc_internal.h"

ASSERT(script_syms_max < u16_max, "ScriptSymId has to be storable as a 16-bit integer");

struct sScriptSymBag {
  Allocator* alloc;
  DynArray   symbols; // ScriptSym[]
};

INLINE_HINT static const ScriptSymData* sym_data(const ScriptSymBag* bag, const ScriptSym id) {
  return &dynarray_begin_t(&bag->symbols, ScriptSymData)[id];
}

INLINE_HINT static bool sym_in_scope(const ScriptSymData* sym, const ScriptPos pos) {
  switch (sym->type) {
  case ScriptSymType_Variable:
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
    switch (sym->type) {
    case ScriptSymType_BuiltinFunction:
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
    switch (sym->type) {
    case ScriptSymType_ExternFunction:
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
    switch (sym->type) {
    case ScriptSymType_Variable:
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
    switch (sym->type) {
    case ScriptSymType_MemoryKey:
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

static void script_sym_clone_into(Allocator* alloc, ScriptSymData* dst, const ScriptSymData* src) {
  *dst = (ScriptSymData){
      .type  = src->type,
      .label = string_dup(alloc, src->label),
      .doc   = string_maybe_dup(alloc, src->doc),
  };
  switch (src->type) {
  case ScriptSymType_BuiltinFunction:
    dst->data.builtinFunc.intr = src->data.builtinFunc.intr;
    if (src->data.builtinFunc.sig) {
      dst->data.builtinFunc.sig = script_sig_clone(alloc, src->data.builtinFunc.sig);
    }
    break;
  case ScriptSymType_ExternFunction:
    dst->data.externFunc = src->data.externFunc;
    break;
  case ScriptSymType_Variable:
    dst->data.var = src->data.var;
    break;
  case ScriptSymType_MemoryKey:
    dst->data.memKey = src->data.memKey;
    break;
  case ScriptSymType_BuiltinConstant:
  case ScriptSymType_Keyword:
  case ScriptSymType_Count:
    break;
  }
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
  script_sym_clear(bag);
  dynarray_destroy(&bag->symbols);
  alloc_free_t(bag->alloc, bag);
}

ScriptSym script_sym_push(ScriptSymBag* bag, const ScriptSymData* sym) {
  diag_assert(!string_is_empty(sym->label));

  const ScriptSym id = (ScriptSym)bag->symbols.size;
  if (UNLIKELY(id == script_syms_max)) {
    return script_sym_sentinel;
  }

  script_sym_clone_into(bag->alloc, dynarray_push_t(&bag->symbols, ScriptSymData), sym);

  return id;
}

void script_sym_clear(ScriptSymBag* bag) {
  dynarray_for_t(&bag->symbols, ScriptSymData, sym) {
    string_free(bag->alloc, sym->label);
    string_maybe_free(bag->alloc, sym->doc);
    switch (sym->type) {
    case ScriptSymType_BuiltinFunction:
      if (sym->data.builtinFunc.sig) {
        script_sig_destroy(sym->data.builtinFunc.sig);
      }
      break;
    case ScriptSymType_ExternFunction:
    case ScriptSymType_Variable:
    case ScriptSymType_MemoryKey:
    case ScriptSymType_BuiltinConstant:
    case ScriptSymType_Keyword:
    case ScriptSymType_Count:
      break;
    }
  }
  dynarray_clear(&bag->symbols);
}

bool script_sym_is_func(const ScriptSymData* sym) {
  return sym->type == ScriptSymType_BuiltinFunction || sym->type == ScriptSymType_ExternFunction;
}

ScriptRange script_sym_location(const ScriptSymData* sym) {
  switch (sym->type) {
  case ScriptSymType_Variable:
    return sym->data.var.location;
  default:
    break;
  }
  return script_range_sentinel;
}

const ScriptSig* script_sym_sig(const ScriptSymData* sym) {
  switch (sym->type) {
  case ScriptSymType_BuiltinFunction:
    return sym->data.builtinFunc.sig;
  default:
    break;
  }
  return null;
}

String script_sym_type_str(const ScriptSymType type) {
  static const String g_names[] = {
      string_static("Keyword"),
      string_static("BuiltinConstant"),
      string_static("BuiltinFunction"),
      string_static("ExternFunction"),
      string_static("Variable"),
      string_static("MemoryKey"),
  };
  ASSERT(array_elems(g_names) == ScriptSymType_Count, "Incorrect number of ScriptSymType names");

  diag_assert(type < ScriptSymType_Count);
  return g_names[type];
}

const ScriptSymData* script_sym_data(const ScriptSymBag* bag, const ScriptSym id) {
  diag_assert_msg(id < bag->symbols.size, "Invalid symbol-id");
  return sym_data(bag, id);
}

ScriptSym script_sym_find(const ScriptSymBag* bag, const ScriptDoc* doc, const ScriptExpr expr) {
  switch (expr_type(doc, expr)) {
  case ScriptExprType_Intrinsic:
    return sym_find_by_intr(bag, expr_data(doc, expr)->intrinsic.intrinsic);
  case ScriptExprType_VarLoad:
    return sym_find_by_var(bag, expr_data(doc, expr)->var_load.var, expr_range(doc, expr).start);
  case ScriptExprType_VarStore:
    return sym_find_by_var(bag, expr_data(doc, expr)->var_store.var, expr_range(doc, expr).start);
  case ScriptExprType_MemLoad:
    return sym_find_by_mem_key(bag, expr_data(doc, expr)->mem_load.key);
  case ScriptExprType_MemStore:
    return sym_find_by_mem_key(bag, expr_data(doc, expr)->mem_store.key);
  case ScriptExprType_Extern:
    return sym_find_by_binder_slot(bag, expr_data(doc, expr)->extern_.func);
  default:
    return script_sym_sentinel;
  }
}

ScriptSym script_sym_first(const ScriptSymBag* bag, const ScriptPos pos) {
  if (!bag->symbols.size) {
    return script_sym_sentinel;
  }
  const ScriptSymData* first = script_sym_data(bag, 0);
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

void script_sym_write(DynString* out, const String sourceText, const ScriptSymData* sym) {
  (void)sourceText;

  fmt_write(out, "[{}] {}", fmt_text(script_sym_type_str(sym->type)), fmt_text(sym->label));
}

String script_sym_scratch(const String sourceText, const ScriptSymData* sym) {
  Mem       bufferMem = alloc_alloc(g_alloc_scratch, usize_kibibyte, 1);
  DynString buffer    = dynstring_create_over(bufferMem);

  script_sym_write(&buffer, sourceText, sym);

  return dynstring_view(&buffer);
}
