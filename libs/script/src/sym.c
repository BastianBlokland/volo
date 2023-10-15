#include "core_alloc.h"
#include "core_array.h"
#include "core_diag.h"
#include "core_dynarray.h"
#include "script_sym.h"

ASSERT(script_syms_max < u16_max, "ScriptSymId has to be storable as a 16-bit integer");

struct sScriptSymBag {
  Allocator* alloc;
  DynArray   symbols; // ScriptSym[]
};

ScriptSymBag* script_sym_bag_create(Allocator* alloc) {
  ScriptSymBag* bag = alloc_alloc_t(alloc, ScriptSymBag);

  *bag = (ScriptSymBag){
      .alloc   = alloc,
      .symbols = dynarray_create_t(alloc, ScriptSym, 128),
  };

  return bag;
}

void script_sym_bag_destroy(ScriptSymBag* bag) {
  dynarray_for_t(&bag->symbols, ScriptSym, sym) { string_free(bag->alloc, sym->label); }
  dynarray_destroy(&bag->symbols);
  alloc_free_t(bag->alloc, bag);
}

ScriptSymId script_sym_push(ScriptSymBag* bag, const ScriptSym* sym) {
  diag_assert(!string_is_empty(sym->label));

  const ScriptSymId id = (ScriptSymId)bag->symbols.size;
  if (UNLIKELY(id == script_syms_max)) {
    return script_sym_sentinel;
  }

  *dynarray_push_t(&bag->symbols, ScriptSym) = (ScriptSym){
      .type  = sym->type,
      .label = string_dup(bag->alloc, sym->label),
  };

  return id;
}

void script_sym_clear(ScriptSymBag* bag) {
  dynarray_for_t(&bag->symbols, ScriptSym, sym) { string_free(bag->alloc, sym->label); }
  dynarray_clear(&bag->symbols);
}

String script_sym_type_str(const ScriptSymType type) {
  static const String g_names[] = {
      string_static("BuiltinConstant"),
      string_static("BuiltinFunction"),
  };
  ASSERT(array_elems(g_names) == ScriptSymType_Count, "Incorrect number of ScriptSymType names");

  diag_assert(type < ScriptSymType_Count);
  return g_names[type];
}

const ScriptSym* script_sym_data(const ScriptSymBag* bag, const ScriptSymId id) {
  diag_assert_msg(id < bag->symbols.size, "Invalid symbol-id");
  return dynarray_at_t(&bag->symbols, id, ScriptSym);
}

ScriptSymId script_sym_first(const ScriptSymBag* bag) {
  return bag->symbols.size ? 0 : script_sym_sentinel;
}

ScriptSymId script_sym_next(const ScriptSymBag* bag, const ScriptSymId itr) {
  if (itr >= (bag->symbols.size - 1)) {
    return script_sym_sentinel;
  }
  return itr + 1;
}
