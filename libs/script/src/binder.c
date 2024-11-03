#include "core_alloc.h"
#include "core_array.h"
#include "core_bits.h"
#include "core_diag.h"
#include "core_intrinsic.h"
#include "core_search.h"
#include "core_sort.h"
#include "core_stringtable.h"
#include "json_read.h"
#include "json_write.h"
#include "script_binder.h"
#include "script_sig.h"
#include "script_val.h"

#define script_binder_aux_chunk_size (4 * usize_kibibyte)

ASSERT(script_binder_max_funcs <= u16_max, "Binder slot needs to be representable by a u16")

static const String g_scriptBinderFlagNames[] = {
    string_static("DisallowMemoryAccess"),
};

struct sScriptBinder {
  Allocator*        alloc;
  Allocator*        allocAux; // (chunked) bump allocator for axillary data (eg signatures).
  String            name;
  String            filter; // File-filter glob pattern.
  ScriptBinderFlags flags : 8;
  bool              finalized;
  u16               count;
  ScriptBinderHash  hash;
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

static ScriptBinderHash binder_hash_compute(const ScriptBinder* binder) {
  u32 hashA = string_maybe_hash(binder->name);
  for (u32 i = 0; i != binder->count; ++i) {
    hashA = bits_hash_32_combine(hashA, binder->names[i]);
  }

  u32 hashB = bits_hash_32_val(binder->flags);
  hashB     = bits_hash_32_combine(hashB, bits_hash_32_val(binder->count));
  return (u64)hashA | ((u64)hashB << 32u);
}

static ScriptVal binder_func_fallback(void* ctx, ScriptBinderCall* call) {
  (void)ctx;
  call->panic = (ScriptPanic){ScriptPanic_UnimplementedBinding};
  return script_null();
}

ScriptBinder*
script_binder_create(Allocator* alloc, const String name, const ScriptBinderFlags flags) {
  ScriptBinder* binder = alloc_alloc_t(alloc, ScriptBinder);

  Allocator* allocAux =
      alloc_chunked_create(alloc, alloc_bump_create, script_binder_aux_chunk_size);

  *binder = (ScriptBinder){
      .alloc    = alloc,
      .allocAux = allocAux,
      .name     = string_maybe_dup(allocAux, name),
      .flags    = flags,
  };

  return binder;
}

void script_binder_destroy(ScriptBinder* binder) {
  alloc_chunked_destroy(binder->allocAux);
  alloc_free_t(binder->alloc, binder);
}

String script_binder_name(const ScriptBinder* binder) { return binder->name; }

ScriptBinderFlags script_binder_flags(const ScriptBinder* binder) { return binder->flags; }

void script_binder_filter_set(ScriptBinder* binder, const String globPattern) {
  // NOTE: The old filter will not be cleaned up from the auxillary data until destruction.
  binder->filter = string_maybe_dup(binder->allocAux, globPattern);
}

String script_binder_filter_get(const ScriptBinder* binder) {
  return string_is_empty(binder->filter) ? string_lit("*") : binder->filter;
}

bool script_binder_match(const ScriptBinder* binder, const String fileIdentifier) {
  String filter = binder->filter;
  if (string_is_empty(filter)) {
    return true; // No filter; always valid.
  }
  // Always start with an implicit wild-card.
  if (*string_begin(filter) != '*') {
    filter = string_combine(g_allocScratch, string_lit("*"), filter);
  }
  return string_match_glob(fileIdentifier, filter, StringMatchFlags_IgnoreCase);
}

void script_binder_declare(
    ScriptBinder*          binder,
    const String           name,
    const String           doc,
    const ScriptSig*       sig,
    const ScriptBinderFunc func) {
  diag_assert(!string_is_empty(name));
  diag_assert_msg(!binder->finalized, "Binder already finalized");
  diag_assert_msg(binder->count < script_binder_max_funcs, "Declared function count exceeds max");

  // TODO: Add error when auxillary allocator runs out of space.

  binder->names[binder->count] = stringtable_add(g_stringtable, name);
  binder->funcs[binder->count] = func ? func : binder_func_fallback;
  binder->docs[binder->count]  = string_maybe_dup(binder->allocAux, doc);
  binder->sigs[binder->count]  = sig ? script_sig_clone(binder->allocAux, sig) : null;
  ++binder->count;
}

void script_binder_finalize(ScriptBinder* binder) {
  diag_assert_msg(!binder->finalized, "Binder already finalized");

  // Compute the binding order (sorted on the name-hash).
  sort_index_quicksort(binder, 0, binder->count, binder_index_compare, binder_index_swap);

  binder->hash      = binder_hash_compute(binder);
  binder->finalized = true;
}

u16 script_binder_count(const ScriptBinder* binder) {
  diag_assert_msg(binder->finalized, "Binder has not been finalized");
  return binder->count;
}

ScriptBinderHash script_binder_hash(const ScriptBinder* binder) {
  diag_assert_msg(binder->finalized, "Binder has not been finalized");
  return binder->hash;
}

ScriptBinderSlot script_binder_slot_lookup(const ScriptBinder* binder, const StringHash nameHash) {
  diag_assert_msg(binder->finalized, "Binder has not been finalized");

  const StringHash* itr = search_binary_t(
      binder->names, binder->names + binder->count, StringHash, compare_stringhash, &nameHash);

  return itr ? (ScriptBinderSlot)(itr - binder->names) : script_binder_slot_sentinel;
}

String script_binder_slot_name(const ScriptBinder* binder, const ScriptBinderSlot slot) {
  diag_assert_msg(binder->finalized, "Binder has not been finalized");
  diag_assert_msg(slot < binder->count, "Invalid slot");

  return stringtable_lookup(g_stringtable, binder->names[slot]);
}

String script_binder_slot_doc(const ScriptBinder* binder, const ScriptBinderSlot slot) {
  diag_assert_msg(binder->finalized, "Binder has not been finalized");
  diag_assert_msg(slot < binder->count, "Invalid slot");

  return binder->docs[slot];
}

const ScriptSig* script_binder_slot_sig(const ScriptBinder* binder, const ScriptBinderSlot slot) {
  diag_assert_msg(binder->finalized, "Binder has not been finalized");
  diag_assert_msg(slot < binder->count, "Invalid slot");

  return binder->sigs[slot];
}

ScriptBinderSlot script_binder_first(const ScriptBinder* binder) {
  diag_assert_msg(binder->finalized, "Binder has not been finalized");
  return binder->count ? 0 : script_binder_slot_sentinel;
}

ScriptBinderSlot script_binder_next(const ScriptBinder* binder, const ScriptBinderSlot itr) {
  diag_assert_msg(binder->finalized, "Binder has not been finalized");
  if (itr >= (binder->count - 1)) {
    return script_binder_slot_sentinel;
  }
  return itr + 1;
}

ScriptVal script_binder_exec(
    const ScriptBinder* binder, const ScriptBinderSlot func, void* ctx, ScriptBinderCall* call) {
  diag_assert_msg(binder->finalized, "Binder has not been finalized");
  diag_assert(func < binder->count);
  return binder->funcs[func](ctx, call);
}

static JsonVal binder_mask_to_json(JsonDoc* d, const ScriptMask mask) {
  if (mask == script_mask_any) {
    return json_add_string(d, string_lit("any"));
  }
  if (intrinsic_popcnt_32(mask) == 1) {
    const ScriptType type = (ScriptType)bitset_next(bitset_from_var(mask), 0);
    return json_add_string(d, script_val_type_str(type));
  }
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
  const String     name = script_binder_slot_name(b, s);
  const String     docu = script_binder_slot_doc(b, s);
  const ScriptSig* sig  = script_binder_slot_sig(b, s);

  const JsonVal obj = json_add_object(d);
  json_add_field_lit(d, obj, "name", json_add_string(d, name));
  json_add_field_lit(d, obj, "doc", json_add_string(d, docu));
  json_add_field_lit(d, obj, "sig", binder_sig_to_json(d, sig));
  return obj;
}

static JsonVal binder_flags_to_json(JsonDoc* d, const ScriptBinderFlags flags) {
  JsonVal arr = json_add_array(d);
  bitset_for(bitset_from_var(flags), flagIndex) {
    diag_assert(flagIndex < array_elems(g_scriptBinderFlagNames));
    json_add_elem(d, arr, json_add_string(d, g_scriptBinderFlagNames[flagIndex]));
  }
  return arr;
}

void script_binder_write(DynString* str, const ScriptBinder* b) {
  diag_assert_msg(b->finalized, "Binder has not been finalized");

  JsonDoc* doc = json_create(g_allocHeap, 512);

  const JsonVal funcsArr = json_add_array(doc);
  for (ScriptBinderSlot slot = 0; slot != b->count; ++slot) {
    json_add_elem(doc, funcsArr, binder_func_to_json(doc, b, slot));
  }

  const JsonVal obj = json_add_object(doc);
  if (!string_is_empty(b->name)) {
    json_add_field_lit(doc, obj, "name", json_add_string(doc, b->name));
  }
  if (b->flags) {
    json_add_field_lit(doc, obj, "flags", binder_flags_to_json(doc, b->flags));
  }
  if (!string_is_empty(b->filter)) {
    json_add_field_lit(doc, obj, "filter", json_add_string(doc, b->filter));
  }
  json_add_field_lit(doc, obj, "functions", funcsArr);

  json_write(str, doc, obj, &json_write_opts(.mode = JsonWriteMode_Compact));
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
  if (sentinel_check(v)) {
    return script_mask_none;
  }
  if (json_type(d, v) == JsonType_String) {
    if (json_string_hash(d, v) == string_hash_lit("any")) {
      return script_mask_any;
    }
    return script_mask(script_val_type_from_hash(json_string_hash(d, v)));
  }
  if (json_type(d, v) == JsonType_Array) {
    ScriptMask ret = 0;
    json_for_elems(d, v, t) {
      if (json_type(d, t) == JsonType_String) {
        ret |= script_mask(script_val_type_from_hash(json_string_hash(d, t)));
      }
    }
    return ret;
  }
  return script_mask_none;
}

static ScriptSigArg binder_arg_from_json(const JsonDoc* d, const JsonVal v) {
  ScriptSigArg arg = {0};
  if (!sentinel_check(v) && json_type(d, v) == JsonType_Object) {
    arg.name = binder_string_from_json(d, json_field_lit(d, v, "name"));
    arg.mask = binder_mask_from_json(d, json_field_lit(d, v, "mask"));
    if (binder_bool_from_json(d, json_field_lit(d, v, "multi"))) {
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
    ret = binder_mask_from_json(d, json_field_lit(d, v, "ret"));

    const JsonVal argsVal = json_field_lit(d, v, "args");
    if (!sentinel_check(argsVal) && json_type(d, argsVal) == JsonType_Array) {
      json_for_elems(d, argsVal, a) { args[argCount++] = binder_arg_from_json(d, a); }
    }
  }
  return script_sig_create(g_allocScratch, ret, args, argCount);
}

static bool binder_func_from_json(ScriptBinder* out, const JsonDoc* d, const JsonVal v) {
  if (sentinel_check(v) || json_type(d, v) != JsonType_Object) {
    return false;
  }
  const String     name = binder_string_from_json(d, json_field_lit(d, v, "name"));
  const String     doc  = binder_string_from_json(d, json_field_lit(d, v, "doc"));
  const ScriptSig* sig  = binder_sig_from_json(d, json_field_lit(d, v, "sig"));
  script_binder_declare(out, name, doc, sig, null);
  return true;
}

static String binder_name_from_json(const JsonDoc* d, const JsonVal nameVal) {
  if (sentinel_check(nameVal) || json_type(d, nameVal) != JsonType_String) {
    return string_empty;
  }
  return json_string(d, nameVal);
}

static ScriptBinderFlags binder_flags_from_json(const JsonDoc* d, const JsonVal flagsVal) {
  ScriptBinderFlags flags = ScriptBinderFlags_None;
  if (sentinel_check(flagsVal) || json_type(d, flagsVal) != JsonType_Array) {
    return flags;
  }
  json_for_elems(d, flagsVal, flagNameVal) {
    if (json_type(d, flagNameVal) == JsonType_String) {
      const StringHash flagNameHash = json_string_hash(d, flagNameVal);
      for (u32 i = 0; i != array_elems(g_scriptBinderFlagNames); ++i) {
        if (flagNameHash == string_hash(g_scriptBinderFlagNames[i])) {
          flags |= 1 << i;
          break;
        }
      }
    }
  }
  return flags;
}

ScriptBinder* script_binder_read(Allocator* alloc, const String str) {
  ScriptBinder* out = null;

  JsonDoc* doc     = json_create(g_allocHeap, 512);
  bool     success = true;

  JsonResult readRes;
  json_read(doc, str, JsonReadFlags_None, &readRes);

  const JsonVal root = readRes.val;
  if (readRes.type != JsonResultType_Success || json_type(doc, root) != JsonType_Object) {
    success = false;
    goto Done;
  }

  const String            name  = binder_name_from_json(doc, json_field_lit(doc, root, "name"));
  const ScriptBinderFlags flags = binder_flags_from_json(doc, json_field_lit(doc, root, "flags"));
  out                           = script_binder_create(alloc, name, flags);

  const JsonVal filterVal = json_field_lit(doc, root, "filter");
  if (!sentinel_check(filterVal) && json_type(doc, filterVal) == JsonType_String) {
    script_binder_filter_set(out, json_string(doc, filterVal));
  }

  const JsonVal funcsVal = json_field_lit(doc, root, "functions");
  if (sentinel_check(funcsVal) || json_type(doc, funcsVal) != JsonType_Array) {
    success = false;
    goto Done;
  }
  json_for_elems(doc, funcsVal, f) { success &= binder_func_from_json(out, doc, f); }

Done:
  json_destroy(doc);
  if (success) {
    script_binder_finalize(out);
    return out;
  }
  if (out) {
    script_binder_destroy(out);
  }
  return null;
}
