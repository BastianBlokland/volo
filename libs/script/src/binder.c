#include "core_alloc.h"
#include "core_bits.h"
#include "core_diag.h"
#include "core_search.h"
#include "core_sort.h"
#include "core_stringtable.h"
#include "json_read.h"
#include "json_write.h"
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
  String            docs[script_binder_max_funcs];
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
  mem_swap(mem_var(binder->docs[a]), mem_var(binder->docs[b]));
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
    string_maybe_free(binder->alloc, binder->docs[i]);
    if (binder->sigs[i]) {
      script_sig_destroy(binder->sigs[i]);
    }
  }
  alloc_free_t(binder->alloc, binder);
}

void script_binder_declare(
    ScriptBinder*          binder,
    const String           name,
    const String           doc,
    const ScriptSig*       sig,
    const ScriptBinderFunc func) {
  diag_assert(!string_is_empty(name));
  diag_assert_msg(!(binder->flags & ScriptBinderFlags_Finalized), "Binder already finalized");
  diag_assert_msg(binder->count < script_binder_max_funcs, "Declared function count exceeds max");

  binder->names[binder->count] = stringtable_add(g_stringtable, name);
  binder->funcs[binder->count] = func;
  binder->docs[binder->count]  = string_maybe_dup(binder->alloc, doc);
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

String script_binder_doc(const ScriptBinder* binder, const ScriptBinderSlot slot) {
  diag_assert_msg(binder->flags & ScriptBinderFlags_Finalized, "Binder has not been finalized");
  diag_assert_msg(slot < binder->count, "Invalid slot");

  return binder->docs[slot];
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

static JsonVal binder_mask_to_json(JsonDoc* doc, const ScriptMask mask) {
  const JsonVal arr = json_add_array(doc);
  bitset_for(bitset_from_var(mask), typeIndex) {
    json_add_elem(doc, arr, json_add_string(doc, script_val_type_str((ScriptType)typeIndex)));
  }
  return arr;
}

static JsonVal binder_sig_arg_to_json(JsonDoc* doc, const ScriptSigArg* arg) {
  const JsonVal obj = json_add_object(doc);
  json_add_field_lit(doc, obj, "name", json_add_string(doc, arg->name));
  json_add_field_lit(doc, obj, "mask", binder_mask_to_json(doc, arg->mask));

  const bool multi = (arg->flags & ScriptSigArgFlags_Multi) != 0;
  json_add_field_lit(doc, obj, "multi", json_add_bool(doc, multi));

  return obj;
}

static JsonVal binder_sig_to_json(JsonDoc* doc, const ScriptSig* sig) {
  const JsonVal obj = json_add_object(doc);

  const JsonVal argsArr = json_add_array(doc);
  for (u8 i = 0; i != script_sig_arg_count(sig); ++i) {
    const ScriptSigArg arg = script_sig_arg(sig, i);
    json_add_elem(doc, argsArr, binder_sig_arg_to_json(doc, &arg));
  }

  json_add_field_lit(doc, obj, "ret", binder_mask_to_json(doc, script_sig_ret(sig)));
  json_add_field_lit(doc, obj, "args", argsArr);
  return obj;
}

static JsonVal
binder_func_to_json(JsonDoc* doc, const ScriptBinder* binder, const ScriptBinderSlot slot) {
  const String     name = script_binder_name(binder, slot);
  const String     docu = script_binder_doc(binder, slot);
  const ScriptSig* sig  = script_binder_sig(binder, slot);

  const JsonVal obj = json_add_object(doc);
  json_add_field_lit(doc, obj, "name", json_add_string(doc, name));
  json_add_field_lit(doc, obj, "doc", json_add_string(doc, docu));
  json_add_field_lit(doc, obj, "sig", binder_sig_to_json(doc, sig));

  return obj;
}

void script_binder_write(DynString* str, const ScriptBinder* binder) {
  diag_assert_msg(binder->flags & ScriptBinderFlags_Finalized, "Binder has not been finalized");

  JsonDoc* doc = json_create(g_alloc_heap, 512);

  const JsonVal funcsArr = json_add_array(doc);
  for (ScriptBinderSlot slot = 0; slot != binder->count; ++slot) {
    json_add_elem(doc, funcsArr, binder_func_to_json(doc, binder, slot));
  }

  const JsonVal rootObj = json_add_object(doc);
  json_add_field_lit(doc, rootObj, "functions", funcsArr);

  json_write(str, doc, rootObj, &json_write_opts());
  json_destroy(doc);
}

static ScriptMask binder_mask_from_json(const JsonDoc* doc, const JsonVal val) {
  if (json_type(doc, val) != JsonType_Array) {
    return script_mask_none;
  }
  ScriptMask ret = 0;
  json_for_elems(doc, val, t) {
    if (json_type(doc, t) == JsonType_String) {
      ret |= 1 << script_val_type_from_hash(string_hash(json_string(doc, t)));
    }
  }
  return ret;
}

static ScriptSigArg binder_sig_arg_from_json(const JsonDoc* doc, const JsonVal val) {
  ScriptSigArg arg = {0};
  if (json_type(doc, val) != JsonType_Object) {
    return arg;
  }
  const JsonVal name = json_field(doc, val, string_lit("name"));
  if (!sentinel_check(name) && json_type(doc, name) == JsonType_String) {
    arg.name = json_string(doc, name);
  }
  const JsonVal mask = json_field(doc, val, string_lit("mask"));
  if (!sentinel_check(mask)) {
    arg.mask = binder_mask_from_json(doc, mask);
  }
  const JsonVal multi = json_field(doc, val, string_lit("multi"));
  if (!sentinel_check(multi) && json_type(doc, multi) == JsonType_Bool && json_bool(doc, multi)) {
    arg.flags |= ScriptSigArgFlags_Multi;
  }
  return arg;
}

static const ScriptSig* binder_sig_from_json(const JsonDoc* doc, const JsonVal val) {
  if (json_type(doc, val) != JsonType_Object) {
    return null;
  }
  const JsonVal maskVal = json_field(doc, val, string_lit("mask"));
  if (sentinel_check(maskVal)) {
    return null;
  }
  const JsonVal argsVal = json_field(doc, val, string_lit("args"));
  if (sentinel_check(argsVal) || json_type(doc, argsVal) != JsonType_Array) {
    return null;
  }
  ScriptSigArg args[script_sig_arg_count_max];
  u8           argCount = 0;
  json_for_elems(doc, argsVal, a) { args[argCount++] = binder_sig_arg_from_json(doc, a); }

  const ScriptMask ret = binder_mask_from_json(doc, maskVal);
  return script_sig_create(g_alloc_scratch, ret, args, argCount);
}

static void binder_func_from_json(ScriptBinder* out, const JsonDoc* doc, const JsonVal val) {
  if (json_type(doc, val) != JsonType_Object) {
    return;
  }
  const JsonVal name = json_field(doc, val, string_lit("name"));
  if (sentinel_check(name) || json_type(doc, name) != JsonType_String) {
    return;
  }
  const JsonVal docu = json_field(doc, val, string_lit("doc"));
  if (sentinel_check(docu) || json_type(doc, docu) != JsonType_String) {
    return;
  }
  const JsonVal sig = json_field(doc, val, string_lit("sig"));
  if (sentinel_check(docu)) {
    return;
  }
  script_binder_declare(
      out, json_string(doc, name), json_string(doc, docu), binder_sig_from_json(doc, sig), null);
}

bool script_binder_read(ScriptBinder* out, const String str) {
  JsonDoc* doc     = json_create(g_alloc_heap, 512);
  bool     success = false;

  JsonResult readRes;
  json_read(doc, str, &readRes);
  if (readRes.type != JsonResultType_Success) {
    goto Ret;
  }
  if (json_type(doc, readRes.val) != JsonType_Object) {
    goto Ret;
  }

  const JsonVal funcsVal = json_field(doc, readRes.val, string_lit("functions"));
  if (sentinel_check(funcsVal) || json_type(doc, funcsVal) != JsonType_Array) {
    goto Ret;
  }

  json_for_elems(doc, funcsVal, f) { binder_func_from_json(out, doc, f); }
  success = true;

Ret:
  json_destroy(doc);
  return success;
}
