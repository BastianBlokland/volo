#include "core_alloc.h"
#include "core_diag.h"
#include "core_dynarray.h"
#include "script_symbol.h"

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

void script_symbol_push(ScriptSymbolBag* bag, const ScriptSymbol* sym) {
  diag_assert(!string_is_empty(sym->label));

  *dynarray_push_t(&bag->symbols, ScriptSymbol) = (ScriptSymbol){
      .type  = sym->type,
      .label = string_dup(bag->alloc, sym->label),
  };
}

void script_symbol_clear(ScriptSymbolBag* bag) {
  dynarray_for_t(&bag->symbols, ScriptSymbol, sym) { string_free(bag->alloc, sym->label); }
  dynarray_clear(&bag->symbols);
}
