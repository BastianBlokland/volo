#include "core_alloc.h"
#include "core_bits.h"
#include "core_diag.h"
#include "core_search.h"
#include "core_sort.h"
#include "script_binder.h"
#include "script_val.h"

#define script_binder_max_funcs 64

typedef enum {
  ScriptBinderFlags_Build = 1 << 0,
} ScriptBinderFlags;

struct sScriptBinder {
  Allocator*        alloc;
  ScriptBinderFlags flags;
  u32               count;
  StringHash        names[script_binder_max_funcs];
  ScriptBinderFunc  funcs[script_binder_max_funcs];
};

ScriptBinder* script_binder_create(Allocator* alloc) {
  ScriptBinder* binder = alloc_alloc_t(alloc, ScriptBinder);
  *binder              = (ScriptBinder){
      .alloc = alloc,
  };
  return binder;
}

void script_binder_destroy(ScriptBinder* binder) { alloc_free_t(binder->alloc, binder); }

void script_binder_bind(ScriptBinder* binder, const StringHash name, const ScriptBinderFunc func) {
  diag_assert_msg(!(binder->flags & ScriptBinderFlags_Build), "Binder already build");
  diag_assert_msg(binder->count < script_binder_max_funcs, "Bound function count exceeds max");

  binder->names[binder->count] = name;
  binder->funcs[binder->count] = func;
  ++binder->count;
}

void script_binder_build(ScriptBinder* binder) {
  diag_assert_msg(!(binder->flags & ScriptBinderFlags_Build), "Binder already build");

  sort_quicksort_t(binder->names, binder->names + binder->count, StringHash, compare_stringhash);
  binder->flags |= ScriptBinderFlags_Build;
}

ScriptBinderSignature script_binder_sig(const ScriptBinder* binder) {
  diag_assert_msg(binder->flags & ScriptBinderFlags_Build, "Binder has not been build");

  u32 funcNameHash = 42;
  for (u32 i = 0; i != binder->count; ++i) {
    funcNameHash = bits_hash_32_combine(funcNameHash, binder->names[i]);
  }

  return (ScriptBinderSignature)((u64)funcNameHash | ((u64)binder->count << 32u));
}

ScriptBinderSlot script_binder_lookup(const ScriptBinder* binder, const StringHash name) {
  diag_assert_msg(binder->flags & ScriptBinderFlags_Build, "Binder has not been build");

  const StringHash* itr = search_binary_t(
      binder->names, binder->names + binder->count, StringHash, compare_stringhash, &name);

  return itr ? (u32)(itr - binder->names) : sentinel_u32;
}

ScriptVal script_binder_exec(
    const ScriptBinder*    binder,
    const ScriptBinderSlot func,
    void*                  ctx,
    ScriptVal*             args,
    const usize            argCount) {
  diag_assert_msg(binder->flags & ScriptBinderFlags_Build, "Binder has not been build");
  diag_assert(func < binder->count);
  return binder->funcs[func](ctx, args, argCount);
}
