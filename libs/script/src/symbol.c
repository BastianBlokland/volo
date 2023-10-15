#include "core_alloc.h"
#include "script_symbol.h"

struct sScriptSymbolBag {
  Allocator* alloc;
};

ScriptSymbolBag* script_symbol_bag_create(Allocator* alloc) {
  ScriptSymbolBag* bag = alloc_alloc_t(alloc, ScriptSymbolBag);
  *bag                 = (ScriptSymbolBag){
                      .alloc = alloc,
  };
  return bag;
}

void script_symbol_bag_destroy(ScriptSymbolBag* bag) { alloc_free_t(bag->alloc, bag); }
