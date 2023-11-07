#include "core_alloc.h"
#include "core_bits.h"
#include "core_diag.h"
#include "core_search.h"
#include "core_sort.h"
#include "core_stringtable.h"
#include "json_read.h"
#include "json_write.h"
#include "script_binder.h"
#include "script_error.h"
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

static ScriptVal binder_func_fallback(void* ctx, const ScriptArgs args, ScriptError* err) {
  (void)ctx;
  (void)args;
  *err = script_error(ScriptError_UnimplementedBinding);
  return script_null();
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
  binder->funcs[binder->count] = func ? func : binder_func_fallback;
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

static JsonVal binder_mask_to_json(JsonDoc* d, const ScriptMask mask) {
  const JsonVal arr = json_add_array(d);
  bitset_for(bitset_from_var(mask), typeIndex) {
    json_add_elem(d, arr, json_add_string(d, script_val_type_str((ScriptType)typeIndex)));
  }
  return arr;
}

static JsonVal binder_arg_to_json(JsonDoc* d, const ScriptSigArg* arg) {
  const JsonVal obj = json_add_object(d);
  json_add_field_lit(d, obj, "name", json_add_string(d, arg->name));
  json_add_field_lit(d, obj, "mask", binder_mask_to_json(d, arg->mask));
  if (arg->flags & ScriptSigArgFlags_Multi) {
    json_add_field_lit(d, obj, "multi", json_add_bool(d, true));
  }
  return obj;
}

static JsonVal binder_sig_to_json(JsonDoc* d, const ScriptSig* sig) {
  const JsonVal argsArr = json_add_array(d);
  for (u8 i = 0; i != script_sig_arg_count(sig); ++i) {
    const ScriptSigArg arg = script_sig_arg(sig, i);
    json_add_elem(d, argsArr, binder_arg_to_json(d, &arg));
  }

  const JsonVal obj = json_add_object(d);
  json_add_field_lit(d, obj, "ret", binder_mask_to_json(d, script_sig_ret(sig)));
  json_add_field_lit(d, obj, "args", argsArr);
  return obj;
}

static JsonVal binder_func_to_json(JsonDoc* d, const ScriptBinder* b, const ScriptBinderSlot s) {
  const String     name = script_binder_name(b, s);
  const String     docu = script_binder_doc(b, s);
  const ScriptSig* sig  = script_binder_sig(b, s);

  const JsonVal obj = json_add_object(d);
  json_add_field_lit(d, obj, "name", json_add_string(d, name));
  json_add_field_lit(d, obj, "doc", json_add_string(d, docu));
  json_add_field_lit(d, obj, "sig", binder_sig_to_json(d, sig));
  return obj;
}

void script_binder_write(DynString* str, const ScriptBinder* b) {
  diag_assert_msg(b->flags & ScriptBinderFlags_Finalized, "Binder has not been finalized");

  JsonDoc* doc = json_create(g_alloc_heap, 512);

  const JsonVal funcsArr = json_add_array(doc);
  for (ScriptBinderSlot slot = 0; slot != b->count; ++slot) {
    json_add_elem(doc, funcsArr, binder_func_to_json(doc, b, slot));
  }

  const JsonVal obj = json_add_object(doc);
  json_add_field_lit(doc, obj, "functions", funcsArr);

  json_write(str, doc, obj, &json_write_opts());
  json_destroy(doc);
}

static String binder_string_from_json(const JsonDoc* d, const JsonVal v) {
  if (!sentinel_check(v) && json_type(d, v) == JsonType_String) {
    return json_string(d, v);
  }
  return string_empty;
}

static bool binder_bool_from_json(const JsonDoc* d, const JsonVal v) {
  if (!sentinel_check(v) && json_type(d, v) == JsonType_Bool) {
    return json_bool(d, v);
  }
  return false;
}

static ScriptMask binder_mask_from_json(const JsonDoc* d, const JsonVal v) {
  ScriptMask ret = 0;
  if (!sentinel_check(v) && json_type(d, v) == JsonType_Array) {
    json_for_elems(d, v, t) {
      if (json_type(d, t) == JsonType_String) {
        ret |= 1 << script_val_type_from_hash(string_hash(json_string(d, t)));
      }
    }
  }
  return ret;
}

static ScriptSigArg binder_arg_from_json(const JsonDoc* d, const JsonVal v) {
  ScriptSigArg arg = {0};
  if (!sentinel_check(v) && json_type(d, v) == JsonType_Object) {
    arg.name = binder_string_from_json(d, json_field(d, v, string_lit("name")));
    arg.mask = binder_mask_from_json(d, json_field(d, v, string_lit("mask")));
    if (binder_bool_from_json(d, json_field(d, v, string_lit("multi")))) {
      arg.flags |= ScriptSigArgFlags_Multi;
    }
  }
  return arg;
}

static const ScriptSig* binder_sig_from_json(const JsonDoc* d, const JsonVal v) {
  ScriptMask   ret = script_mask_none;
  ScriptSigArg args[script_sig_arg_count_max];
  u8           argCount = 0;
  if (!sentinel_check(v) && json_type(d, v) == JsonType_Object) {
    ret = binder_mask_from_json(d, json_field(d, v, string_lit("ret")));

    const JsonVal argsVal = json_field(d, v, string_lit("args"));
    if (!sentinel_check(argsVal) && json_type(d, argsVal) == JsonType_Array) {
      json_for_elems(d, argsVal, a) { args[argCount++] = binder_arg_from_json(d, a); }
    }
  }
  return script_sig_create(g_alloc_scratch, ret, args, argCount);
}

static void binder_func_from_json(ScriptBinder* out, const JsonDoc* d, const JsonVal v) {
  if (!sentinel_check(v) && json_type(d, v) == JsonType_Object) {
    const String     name = binder_string_from_json(d, json_field(d, v, string_lit("name")));
    const String     doc  = binder_string_from_json(d, json_field(d, v, string_lit("doc")));
    const ScriptSig* sig  = binder_sig_from_json(d, json_field(d, v, string_lit("sig")));
    script_binder_declare(out, name, doc, sig, null);
  }
}

bool script_binder_read(ScriptBinder* out, const String str) {
  JsonDoc* doc     = json_create(g_alloc_heap, 512);
  bool     success = false;

  JsonResult readRes;
  json_read(doc, str, &readRes);

  if (readRes.type == JsonResultType_Success && json_type(doc, readRes.val) == JsonType_Object) {
    const JsonVal funcsVal = json_field(doc, readRes.val, string_lit("functions"));
    if (!sentinel_check(funcsVal) && json_type(doc, funcsVal) == JsonType_Array) {
      json_for_elems(doc, funcsVal, f) { binder_func_from_json(out, doc, f); }
    }
    success = true;
  }

  json_destroy(doc);
  return success;
}
