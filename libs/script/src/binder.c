#include "core_alloc.h"
#include "core_diag.h"
#include "script_binder.h"

struct sScriptBinder {
  Allocator* alloc;
};

ScriptBinder* script_binder_create(Allocator* alloc) {
  ScriptBinder* binder = alloc_alloc_t(alloc, ScriptBinder);
  *binder              = (ScriptBinder){
      .alloc = alloc,
  };
  return binder;
}

void script_binder_destroy(ScriptBinder* binder) { alloc_free_t(binder->alloc, binder); }
