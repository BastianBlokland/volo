#include "core_alloc.h"
#include "core_base64.h"
#include "core_bits.h"
#include "core_diag.h"
#include "core_float.h"
#include "core_stringtable.h"
#include "data_write.h"
#include "json_write.h"

#include "registry_internal.h"

typedef struct {
  const DataWriteJsonOpts* opts;
  const DataReg*           reg;
  JsonDoc*                 doc;
  const DataMeta           meta;
  Mem                      data;
  bool                     skipOptional;
} WriteCtx;

static JsonVal data_write_json_val(const WriteCtx*);

static JsonVal data_write_json_bool(const WriteCtx* ctx) {
  const bool val = *mem_as_t(ctx->data, bool);
  if (ctx->skipOptional && ctx->meta.flags & DataFlags_Opt && !val) {
    return sentinel_u32;
  }
  return json_add_bool(ctx->doc, val);
}

static JsonVal data_write_json_number(const WriteCtx* ctx) {
  if (ctx->skipOptional && ctx->meta.flags & DataFlags_Opt && mem_all(ctx->data, 0)) {
    return sentinel_u32;
  }

#define RET_ADD_NUM(_T_)                                                                           \
  case DataKind_##_T_:                                                                             \
    return json_add_number(ctx->doc, (f64)*mem_as_t(ctx->data, _T_))

  switch (data_decl(ctx->reg, ctx->meta.type)->kind) {
    RET_ADD_NUM(i8);
    RET_ADD_NUM(i16);
    RET_ADD_NUM(i32);
    RET_ADD_NUM(i64);
    RET_ADD_NUM(u8);
    RET_ADD_NUM(u16);
    RET_ADD_NUM(u32);
    RET_ADD_NUM(u64);
    RET_ADD_NUM(f32);
    RET_ADD_NUM(f64);
  case DataKind_f16: {
    const f32 val = float_f16_to_f32(*mem_as_t(ctx->data, f16));
    return json_add_number(ctx->doc, (f64)val);
  }
  default:
    diag_crash();
  }

#undef RET_ADD_NUM
}

static JsonVal data_write_json_string(const WriteCtx* ctx) {
  const String val = *mem_as_t(ctx->data, String);
  if (ctx->skipOptional && ctx->meta.flags & DataFlags_Opt && string_is_empty(val)) {
    return sentinel_u32;
  }
  return json_add_string(ctx->doc, val);
}

static JsonVal data_write_json_string_hash(const WriteCtx* ctx) {
  const StringHash val = *mem_as_t(ctx->data, StringHash);
  if (ctx->skipOptional && ctx->meta.flags & DataFlags_Opt && !val) {
    return sentinel_u32;
  }
  if (!val) {
    return json_add_string(ctx->doc, string_empty);
  }
  const String valStr = stringtable_lookup(g_stringtable, val);
  if (!string_is_empty(valStr)) {
    return json_add_string(ctx->doc, valStr);
  }
  return json_add_number(ctx->doc, (f64)val);
}

static JsonVal data_write_json_mem(const WriteCtx* ctx) {
  const DataMem val = *mem_as_t(ctx->data, DataMem);
  if (!mem_valid(data_mem(val))) {
    if (ctx->skipOptional && ctx->meta.flags & DataFlags_Opt) {
      return sentinel_u32;
    }
    return json_add_string(ctx->doc, string_empty);
  }

  /**
   * Encode the memory as MIME base64 and add it as a string to the json document.
   *
   * TODO: Instead of 'json_add_string' copying the encoded data once again we could encode directly
   * into a string owned by the json document.
   */
  const usize base64Size   = base64_encoded_size(data_mem(val));
  const bool  useScratch   = base64Size < (64 * usize_kibibyte);
  Allocator*  bufferAlloc  = useScratch ? g_allocScratch : g_allocHeap;
  const Mem   base64Buffer = alloc_alloc(bufferAlloc, base64Size, 1);
  DynString   base64Str    = dynstring_create_over(base64Buffer);

  base64_encode(&base64Str, data_mem(val));

  const JsonVal ret = json_add_string(ctx->doc, dynstring_view(&base64Str));
  alloc_free(bufferAlloc, base64Buffer);
  return ret;
}

static void data_write_json_struct_to_obj(const WriteCtx* ctx, const JsonVal jsonObj) {
  const DataDecl* decl = data_decl(ctx->reg, ctx->meta.type);

  dynarray_for_t(&decl->val_struct.fields, DataDeclField, fieldDecl) {
    const WriteCtx fieldCtx = {
        .reg          = ctx->reg,
        .doc          = ctx->doc,
        .meta         = fieldDecl->meta,
        .data         = data_field_mem(ctx->reg, fieldDecl, ctx->data),
        .skipOptional = true,
    };
    const JsonVal fieldVal = data_write_json_val(&fieldCtx);
    if (sentinel_check(fieldVal)) {
      continue;
    }
    json_add_field_str(ctx->doc, jsonObj, fieldDecl->id.name, fieldVal);
  }
}

static JsonVal data_write_json_struct(const WriteCtx* ctx) {
  const JsonVal jsonObj = json_add_object(ctx->doc);
  data_write_json_struct_to_obj(ctx, jsonObj);
  return jsonObj;
}

static JsonVal data_write_json_union(const WriteCtx* ctx) {
  const JsonVal   jsonObj = json_add_object(ctx->doc);
  const DataDecl* decl    = data_decl(ctx->reg, ctx->meta.type);
  const i32       tag     = *data_union_tag(&decl->val_union, ctx->data);

  const DataDeclChoice* choice = data_choice_from_tag(&decl->val_union, tag);
  diag_assert(choice);

  const JsonVal typeStr = json_add_string(ctx->doc, choice->id.name);
  json_add_field_lit(ctx->doc, jsonObj, "$type", typeStr);

  const String* name = data_union_name(&decl->val_union, ctx->data);
  if (name) {
    json_add_field_lit(ctx->doc, jsonObj, "$name", json_add_string(ctx->doc, *name));
  }

  const bool emptyChoice = choice->meta.type == 0;
  if (!emptyChoice) {
    const WriteCtx choiceCtx = {
        .reg  = ctx->reg,
        .doc  = ctx->doc,
        .meta = choice->meta,
        .data = data_choice_mem(ctx->reg, choice, ctx->data),
    };
    switch (data_decl(ctx->reg, choice->meta.type)->kind) {
    case DataKind_Struct:
      /**
       * Inline the struct fields into the current json object.
       */
      data_write_json_struct_to_obj(&choiceCtx, jsonObj);
      break;
    default:
      json_add_field_lit(ctx->doc, jsonObj, "$data", data_write_json_val(&choiceCtx));
      break;
    }
  }
  return jsonObj;
}

static JsonVal data_write_json_enum(const WriteCtx* ctx) {
  const DataDecl* decl = data_decl(ctx->reg, ctx->meta.type);
  const i32       val  = *mem_as_t(ctx->data, i32);

  if (ctx->skipOptional && ctx->meta.flags & DataFlags_Opt && !val) {
    return sentinel_u32;
  }

  if (decl->val_enum.multi) {
    const JsonVal jsonArray = json_add_array(ctx->doc);

    bitset_for(bitset_from_var(val), bit) {
      const DataDeclConst* bitConst = data_const_from_val(&decl->val_enum, 1 << bit);
      if (bitConst) {
        json_add_elem(ctx->doc, jsonArray, json_add_string(ctx->doc, bitConst->id.name));
      } else {
        json_add_elem(ctx->doc, jsonArray, json_add_number(ctx->doc, bit));
      }
    }

    return jsonArray;
  }

  const DataDeclConst* constDecl = data_const_from_val(&decl->val_enum, val);
  if (constDecl) {
    return json_add_string(ctx->doc, constDecl->id.name);
  }

  return json_add_number(ctx->doc, val);
}

static JsonVal data_write_json_val_single(const WriteCtx* ctx) {
  switch (data_decl(ctx->reg, ctx->meta.type)->kind) {
  case DataKind_bool:
    return data_write_json_bool(ctx);
  case DataKind_i8:
  case DataKind_i16:
  case DataKind_i32:
  case DataKind_i64:
  case DataKind_u8:
  case DataKind_u16:
  case DataKind_u32:
  case DataKind_u64:
  case DataKind_f16:
  case DataKind_f32:
  case DataKind_f64:
    return data_write_json_number(ctx);
  case DataKind_String:
    return data_write_json_string(ctx);
  case DataKind_StringHash:
    return data_write_json_string_hash(ctx);
  case DataKind_DataMem:
    return data_write_json_mem(ctx);
  case DataKind_Struct:
    return data_write_json_struct(ctx);
  case DataKind_Union:
    return data_write_json_union(ctx);
  case DataKind_Enum:
    return data_write_json_enum(ctx);
  case DataKind_Invalid:
  case DataKind_Count:
    break;
  }
  diag_crash();
}

static JsonVal data_write_json_val_pointer(const WriteCtx* ctx) {
  void* ptr = *mem_as_t(ctx->data, void*);
  if (!ptr) {
    return json_add_null(ctx->doc);
  }
  const DataDecl* decl   = data_decl(ctx->reg, ctx->meta.type);
  const WriteCtx  subCtx = {
       .reg  = ctx->reg,
       .doc  = ctx->doc,
       .meta = data_meta_base(ctx->meta),
       .data = mem_create(ptr, decl->size),
  };
  return data_write_json_val_single(&subCtx);
}

static JsonVal data_write_json_val_inline_array(const WriteCtx* ctx) {
  if (UNLIKELY(!ctx->meta.fixedCount)) {
    diag_crash_msg("Inline-arrays need at least 1 entry");
  }
  if (UNLIKELY(ctx->data.size != data_meta_size(ctx->reg, ctx->meta))) {
    diag_crash_msg("Unexpected data-size for inline array");
  }
  const JsonVal   jsonArray = json_add_array(ctx->doc);
  const DataDecl* decl      = data_decl(ctx->reg, ctx->meta.type);

  for (u16 i = 0; i != ctx->meta.fixedCount; ++i) {
    const WriteCtx elemCtx = {
        .reg  = ctx->reg,
        .doc  = ctx->doc,
        .meta = data_meta_base(ctx->meta),
        .data = mem_create(bits_ptr_offset(ctx->data.ptr, decl->size * i), decl->size),
    };
    const JsonVal elemVal = data_write_json_val_single(&elemCtx);
    json_add_elem(ctx->doc, jsonArray, elemVal);
  }
  return jsonArray;
}

static JsonVal data_write_json_val_array(const WriteCtx* ctx) {
  const JsonVal    jsonArray = json_add_array(ctx->doc);
  const DataDecl*  decl      = data_decl(ctx->reg, ctx->meta.type);
  const DataArray* array     = mem_as_t(ctx->data, DataArray);

  for (usize i = 0; i != array->count; ++i) {
    const WriteCtx elemCtx = {
        .reg  = ctx->reg,
        .doc  = ctx->doc,
        .meta = data_meta_base(ctx->meta),
        .data = data_elem_mem(decl, array, i),
    };
    const JsonVal elemVal = data_write_json_val_single(&elemCtx);
    json_add_elem(ctx->doc, jsonArray, elemVal);
  }
  return jsonArray;
}

static JsonVal data_write_json_val_dynarray(const WriteCtx* ctx) {
  const JsonVal   jsonArray = json_add_array(ctx->doc);
  const DynArray* array     = mem_as_t(ctx->data, DynArray);

  for (usize i = 0; i != array->size; ++i) {
    const WriteCtx elemCtx = {
        .reg  = ctx->reg,
        .doc  = ctx->doc,
        .meta = data_meta_base(ctx->meta),
        .data = dynarray_at(array, i, 1),
    };
    const JsonVal elemVal = data_write_json_val_single(&elemCtx);
    json_add_elem(ctx->doc, jsonArray, elemVal);
  }
  return jsonArray;
}

static JsonVal data_write_json_val(const WriteCtx* ctx) {
  switch (ctx->meta.container) {
  case DataContainer_None:
    return data_write_json_val_single(ctx);
  case DataContainer_Pointer:
    return data_write_json_val_pointer(ctx);
  case DataContainer_InlineArray:
    return data_write_json_val_inline_array(ctx);
  case DataContainer_DataArray:
    return data_write_json_val_array(ctx);
  case DataContainer_DynArray:
    return data_write_json_val_dynarray(ctx);
  }
  diag_crash();
}

void data_write_json(
    const DataReg*           reg,
    DynString*               str,
    const DataMeta           meta,
    const Mem                data,
    const DataWriteJsonOpts* opts) {
  JsonDoc*       doc = json_create(g_allocHeap, 512);
  const WriteCtx ctx = {
      .opts = opts,
      .reg  = reg,
      .doc  = doc,
      .meta = meta,
      .data = data,
  };
  const JsonVal val = data_write_json_val(&ctx);

  const JsonWriteOpts jsonOpts = json_write_opts(
          .numberMaxDecDigits    = opts->numberMaxDecDigits,
          .numberExpThresholdPos = opts->numberExpThresholdPos,
          .numberExpThresholdNeg = opts->numberExpThresholdNeg,
          .mode                  = opts->compact ? JsonWriteMode_Compact : JsonWriteMode_Verbose);

  json_write(str, doc, val, &jsonOpts);
  json_destroy(doc);
}
