#include "core_alloc.h"
#include "core_compare.h"
#include "core_diag.h"
#include "core_search.h"
#include "core_sort.h"
#include "script_binder.h"

#define script_binder_max_funcs 64

typedef enum {
  ScriptBinderFlags_Prepared = 1 << 0,
} ScriptBinderFlags;

struct sScriptBinder {
  Allocator*        alloc;
  ScriptBinderFlags flags;
  u32               count;
  StringHash        names[script_binder_max_funcs];
};

ScriptBinder* script_binder_create(Allocator* alloc) {
  ScriptBinder* binder = alloc_alloc_t(alloc, ScriptBinder);
  *binder              = (ScriptBinder){
      .alloc = alloc,
  };
  return binder;
}

void script_binder_destroy(ScriptBinder* binder) { alloc_free_t(binder->alloc, binder); }

void script_binder_declare(ScriptBinder* binder, const StringHash name) {
  diag_assert_msg(!(binder->flags & ScriptBinderFlags_Prepared), "Binder already prepared");
  diag_assert_msg(binder->count < script_binder_max_funcs, "Bound function count exceeds max");

  binder->names[binder->count++] = name;
}

void script_binder_prepare(ScriptBinder* binder) {
  diag_assert_msg(!(binder->flags & ScriptBinderFlags_Prepared), "Binder already prepared");

  sort_quicksort_t(binder->names, binder->names + binder->count, StringHash, compare_stringhash);
  binder->flags |= ScriptBinderFlags_Prepared;
}

ScriptBindSlot script_binder_lookup(const ScriptBinder* binder, const StringHash name) {
  diag_assert_msg(binder->flags & ScriptBinderFlags_Prepared, "Binder has not been prepared");

  const StringHash* itr = search_binary_t(
      binder->names, binder->names + binder->count, StringHash, compare_stringhash, &name);

  return itr ? (u32)(itr - binder->names) : sentinel_u32;
}
