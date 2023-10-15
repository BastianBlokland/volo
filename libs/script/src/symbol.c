#include "core_alloc.h"
#include "core_diag.h"
#include "core_dynarray.h"
#include "script_symbol.h"

ASSERT(script_symbols_max < u16_max, "ScriptSymbolId has to be storable as a 16-bit integer");

struct sScriptSymbolBag {
  Allocator* alloc;
  DynArray   symbols; // ScriptSymbol[]
};

ScriptSymbolBag* script_symbol_bag_create(Allocator* alloc) {
  ScriptSymbolBag* bag = alloc_alloc_t(alloc, ScriptSymbolBag);

  *bag = (ScriptSymbolBag){
      .alloc   = alloc,
      .symbols = dynarray_create_t(alloc, ScriptSymbol, 128),
  };

  return bag;
}

void script_symbol_bag_destroy(ScriptSymbolBag* bag) {
  dynarray_for_t(&bag->symbols, ScriptSymbol, sym) { string_free(bag->alloc, sym->label); }
  dynarray_destroy(&bag->symbols);
  alloc_free_t(bag->alloc, bag);
}

ScriptSymbolId script_symbol_push(ScriptSymbolBag* bag, const ScriptSymbol* sym) {
  diag_assert(!string_is_empty(sym->label));

  const ScriptSymbolId id = (ScriptSymbolId)bag->symbols.size;
  if (UNLIKELY(id == script_symbols_max)) {
    return script_symbol_sentinel;
  }

  *dynarray_push_t(&bag->symbols, ScriptSymbol) = (ScriptSymbol){
      .type  = sym->type,
      .label = string_dup(bag->alloc, sym->label),
  };

  return id;
}

void script_symbol_clear(ScriptSymbolBag* bag) {
  dynarray_for_t(&bag->symbols, ScriptSymbol, sym) { string_free(bag->alloc, sym->label); }
  dynarray_clear(&bag->symbols);
}

const ScriptSymbol* script_symbol_data(const ScriptSymbolBag* bag, const ScriptSymbolId id) {
  diag_assert_msg(id < bag->symbols.size, "Invalid symbol-id");
  return dynarray_at_t(&bag->symbols, id, ScriptSymbol);
}

ScriptSymbolId script_symbol_first(const ScriptSymbolBag* bag) {
  return bag->symbols.size ? 0 : script_symbol_sentinel;
}

ScriptSymbolId script_symbol_next(const ScriptSymbolBag* bag, const ScriptSymbolId itr) {
  if (itr >= (bag->symbols.size - 1)) {
    return script_symbol_sentinel;
  }
  return itr + 1;
}
