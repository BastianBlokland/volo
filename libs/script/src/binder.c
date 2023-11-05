#include "core_alloc.h"
#include "core_bits.h"
#include "core_diag.h"
#include "core_search.h"
#include "core_sort.h"
#include "core_stringtable.h"
#include "script_binder.h"
#include "script_sig.h"
#include "script_val.h"

#define script_binder_max_funcs 64

ASSERT(script_binder_max_funcs <= u16_max, "Binder slot needs to be representable by a u16")

typedef enum {
  ScriptBinderFlags_Finalized = 1 << 0,
} ScriptBinderFlags;

struct sScriptBinder {
  Allocator*        alloc;
  ScriptBinderFlags flags;
  u16               count;
  ScriptBinderFunc  funcs[script_binder_max_funcs];
  StringHash        names[script_binder_max_funcs];
  ScriptSig*        sigs[script_binder_max_funcs];
};

static i8 binder_index_compare(const void* ctx, const usize a, const usize b) {
  const ScriptBinder* binder = ctx;
  return compare_stringhash(binder->names + a, binder->names + b);
}

static void binder_index_swap(void* ctx, const usize a, const usize b) {
  ScriptBinder* binder = ctx;
  mem_swap(mem_var(binder->names[a]), mem_var(binder->names[b]));
  mem_swap(mem_var(binder->funcs[a]), mem_var(binder->funcs[b]));
  mem_swap(mem_var(binder->sigs[a]), mem_var(binder->sigs[b]));
}

ScriptBinder* script_binder_create(Allocator* alloc) {
  ScriptBinder* binder = alloc_alloc_t(alloc, ScriptBinder);

  *binder = (ScriptBinder){
      .alloc = alloc,
  };

  return binder;
}

void script_binder_destroy(ScriptBinder* binder) {
  for (u16 i = 0; i != binder->count; ++i) {
    if (binder->sigs[i]) {
      script_sig_destroy(binder->sigs[i]);
    }
  }
  alloc_free_t(binder->alloc, binder);
}

void script_binder_declare(
    ScriptBinder* binder, const String nameStr, const ScriptSig* sig, const ScriptBinderFunc func) {
  diag_assert_msg(!(binder->flags & ScriptBinderFlags_Finalized), "Binder already finalized");
  diag_assert_msg(binder->count < script_binder_max_funcs, "Declared function count exceeds max");

  binder->names[binder->count] = stringtable_add(g_stringtable, nameStr);
  binder->funcs[binder->count] = func;
  binder->sigs[binder->count]  = sig ? script_sig_clone(binder->alloc, sig) : null;
  ++binder->count;
}

void script_binder_finalize(ScriptBinder* binder) {
  diag_assert_msg(!(binder->flags & ScriptBinderFlags_Finalized), "Binder already finalized");

  // Compute the binding order (sorted on the name-hash).
  sort_index_quicksort(binder, 0, binder->count, binder_index_compare, binder_index_swap);

  binder->flags |= ScriptBinderFlags_Finalized;
}

u16 script_binder_count(const ScriptBinder* binder) {
  diag_assert_msg(binder->flags & ScriptBinderFlags_Finalized, "Binder has not been finalized");
  return binder->count;
}

ScriptBinderHash script_binder_hash(const ScriptBinder* binder) {
  diag_assert_msg(binder->flags & ScriptBinderFlags_Finalized, "Binder has not been finalized");

  u32 funcNameHash = 42;
  for (u32 i = 0; i != binder->count; ++i) {
    funcNameHash = bits_hash_32_combine(funcNameHash, binder->names[i]);
  }

  return (ScriptBinderHash)((u64)funcNameHash | ((u64)binder->count << 32u));
}

ScriptBinderSlot script_binder_lookup(const ScriptBinder* binder, const StringHash nameHash) {
  diag_assert_msg(binder->flags & ScriptBinderFlags_Finalized, "Binder has not been finalized");

  const StringHash* itr = search_binary_t(
      binder->names, binder->names + binder->count, StringHash, compare_stringhash, &nameHash);

  return itr ? (ScriptBinderSlot)(itr - binder->names) : script_binder_slot_sentinel;
}

String script_binder_name(const ScriptBinder* binder, const ScriptBinderSlot slot) {
  diag_assert_msg(binder->flags & ScriptBinderFlags_Finalized, "Binder has not been finalized");
  diag_assert_msg(slot < binder->count, "Invalid slot");

  // TODO: Using the global string-table for this is kinda questionable.
  return stringtable_lookup(g_stringtable, binder->names[slot]);
}

const ScriptSig* script_binder_sig(const ScriptBinder* binder, const ScriptBinderSlot slot) {
  diag_assert_msg(binder->flags & ScriptBinderFlags_Finalized, "Binder has not been finalized");
  diag_assert_msg(slot < binder->count, "Invalid slot");

  return binder->sigs[slot];
}

ScriptBinderSlot script_binder_first(const ScriptBinder* binder) {
  diag_assert_msg(binder->flags & ScriptBinderFlags_Finalized, "Binder has not been finalized");
  return binder->count ? 0 : script_binder_slot_sentinel;
}

ScriptBinderSlot script_binder_next(const ScriptBinder* binder, const ScriptBinderSlot itr) {
  diag_assert_msg(binder->flags & ScriptBinderFlags_Finalized, "Binder has not been finalized");
  if (itr >= (binder->count - 1)) {
    return script_binder_slot_sentinel;
  }
  return itr + 1;
}

ScriptVal script_binder_exec(
    const ScriptBinder*    binder,
    const ScriptBinderSlot func,
    void*                  ctx,
    const ScriptArgs       args,
    ScriptError*           err) {
  diag_assert_msg(binder->flags & ScriptBinderFlags_Finalized, "Binder has not been finalized");
  diag_assert(func < binder->count);
  return binder->funcs[func](ctx, args, err);
}
