#include "core_alloc.h"
#include "core_bits.h"
#include "core_diag.h"
#include "core_search.h"
#include "core_sort.h"
#include "core_stringtable.h"
#include "script_binder.h"
#include "script_val.h"

#define script_binder_max_funcs 64

ASSERT(script_binder_max_funcs <= u16_max, "Binder slot needs to be representable by a u16")

typedef struct {
  StringHash       name;
  ScriptBinderFunc func;
} BinderSortEntry;

static i8 script_binder_compare_entry(const void* a, const void* b) {
  const StringHash nameA = *field_ptr(a, BinderSortEntry, name);
  const StringHash nameB = *field_ptr(b, BinderSortEntry, name);
  return nameA < nameB ? -1 : nameA > nameB ? 1 : 0;
}

typedef enum {
  ScriptBinderFlags_Finalized = 1 << 0,
} ScriptBinderFlags;

struct sScriptBinder {
  Allocator*        alloc;
  ScriptBinderFlags flags;
  u16               count;
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

void script_binder_declare(
    ScriptBinder* binder, const String nameStr, const ScriptBinderFunc func) {
  diag_assert_msg(!(binder->flags & ScriptBinderFlags_Finalized), "Binder already finalized");
  diag_assert_msg(binder->count < script_binder_max_funcs, "Declared function count exceeds max");

  binder->names[binder->count] = stringtable_add(g_stringtable, nameStr);
  binder->funcs[binder->count] = func;
  ++binder->count;
}

void script_binder_finalize(ScriptBinder* binder) {
  diag_assert_msg(!(binder->flags & ScriptBinderFlags_Finalized), "Binder already finalized");

  // Compute the binding order (sorted on the name-hash).
  BinderSortEntry* entries = alloc_array_t(g_alloc_scratch, BinderSortEntry, binder->count);
  for (u16 i = 0; i != binder->count; ++i) {
    entries[i] = (BinderSortEntry){.name = binder->names[i], .func = binder->funcs[i]};
  }
  sort_bubblesort_t(entries, entries + binder->count, BinderSortEntry, script_binder_compare_entry);

  // Re-order the names and functions to match the binding order.
  for (u16 i = 0; i != binder->count; ++i) {
    binder->names[i] = entries[i].name;
    binder->funcs[i] = entries[i].func;
  }

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

ScriptBinderSlot script_binder_lookup(const ScriptBinder* binder, const StringHash name) {
  diag_assert_msg(binder->flags & ScriptBinderFlags_Finalized, "Binder has not been finalized");

  const StringHash* itr = search_binary_t(
      binder->names, binder->names + binder->count, StringHash, compare_stringhash, &name);

  return itr ? (ScriptBinderSlot)(itr - binder->names) : script_binder_slot_sentinel;
}

String script_binder_name_str(const ScriptBinder* binder, const ScriptBinderSlot slot) {
  diag_assert_msg(binder->flags & ScriptBinderFlags_Finalized, "Binder has not been finalized");
  diag_assert_msg(slot < binder->count, "Invalid slot");

  // TODO: Using the global string-table for this is kinda questionable.
  return stringtable_lookup(g_stringtable, binder->names[slot]);
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
