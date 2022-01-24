#include "core_alloc.h"
#include "core_annotation.h"
#include "core_bits.h"
#include "core_diag.h"
#include "data_read.h"
#include "json_read.h"

#include "registry_internal.h"

#define result_success()                                                                           \
  (DataReadResult) { 0 }

#define result_fail(_ERR_, _MSG_FORMAT_LIT_, ...)                                                  \
  (DataReadResult) {                                                                               \
    .error = (_ERR_), .errorMsg = fmt_write_scratch(_MSG_FORMAT_LIT_, __VA_ARGS__)                 \
  }

typedef struct {
  const DataReg* reg;
  Allocator*     alloc;
  DynArray*      allocations;
  const JsonDoc* doc;
  const JsonVal  val;
  const DataMeta meta;
  Mem            data;
} ReadCtx;

static void data_read_json_val(const ReadCtx*, DataReadResult*);

/**
 * Track allocations so they can be undone in case of an error.
 */
static void data_register_alloc(const ReadCtx* ctx, const Mem allocation) {
  *dynarray_push_t(ctx->allocations, Mem) = allocation;
}

static bool data_check_type(const ReadCtx* ctx, const JsonType jsonType, DataReadResult* res) {
  if (UNLIKELY(jsonType != json_type(ctx->doc, ctx->val))) {
    *res = result_fail(
        DataReadError_MismatchedType,
        "Expected json {} got {}",
        fmt_text(json_type_str(jsonType)),
        fmt_text(json_type_str(json_type(ctx->doc, ctx->val))));
    return false;
  }
  *res = result_success();
  return true;
}

/**
 * Get the minimal representable number for the given DataKind.
 */
static f64 data_number_min(const DataKind kind) {
  static const f64 g_minValue[DataKind_Count] = {
      [DataKind_i8]  = i8_min,
      [DataKind_i16] = i16_min,
      [DataKind_i32] = i32_min,
      [DataKind_i64] = i64_min,
      [DataKind_f32] = f32_min,
      [DataKind_f64] = f64_min,
  };
  return g_minValue[kind];
}

/**
 * Get the maximum representable number for the given DataKind.
 */
static f64 data_number_max(const DataKind kind) {
  static const f64 g_maxValue[DataKind_Count] = {
      [DataKind_u8]  = u8_max,
      [DataKind_u16] = u16_max,
      [DataKind_u32] = u32_max,
      [DataKind_u64] = (f64)u64_max,
      [DataKind_i8]  = i8_max,
      [DataKind_i16] = i16_max,
      [DataKind_i32] = i32_max,
      [DataKind_i64] = (f64)i64_max,
      [DataKind_f32] = f32_max,
      [DataKind_f64] = f64_max,
  };
  return g_maxValue[kind];
}

static void data_read_json_number(const ReadCtx* ctx, DataReadResult* res) {
  if (UNLIKELY(!data_check_type(ctx, JsonType_Number, res))) {
    return;
  }
  const DataDecl* decl   = data_decl(ctx->reg, ctx->meta.type);
  const f64       number = json_number(ctx->doc, ctx->val);

  const f64 min = data_number_min(decl->kind);
  if (UNLIKELY(number < min)) {
    *res = result_fail(
        DataReadError_NumberOutOfBounds,
        "Value {} is smaller then the minimum of {}",
        fmt_float(number),
        fmt_float(min));
    return;
  }

  const f64 max = data_number_max(decl->kind);
  if (UNLIKELY(number > max)) {
    *res = result_fail(
        DataReadError_NumberOutOfBounds,
        "Value {} is bigger then the maximum of {}",
        fmt_float(number),
        fmt_float(max));
    return;
  }

  // clang-format off
#define READ_PRIM_NUM(_T_) case DataKind_##_T_: *mem_as_t(ctx->data, _T_) = (_T_)number;

  switch (decl->kind) {
    READ_PRIM_NUM(i8);  break;
    READ_PRIM_NUM(i16); break;
    READ_PRIM_NUM(i32); break;
    READ_PRIM_NUM(i64); break;
    READ_PRIM_NUM(u8);  break;
    READ_PRIM_NUM(u16); break;
    READ_PRIM_NUM(u32); break;
    READ_PRIM_NUM(u64); break;
    READ_PRIM_NUM(f32); break;
    READ_PRIM_NUM(f64); break;
  default:
    diag_crash();
  }
  // clang-format on
#undef READ_PRIM_NUM

  if (UNLIKELY(ctx->meta.flags & DataFlags_NotEmpty && mem_all(ctx->data, 0))) {
    *res = result_fail(DataReadError_ZeroIsInvalid, "Value cannot be zero");
  } else {
    *res = result_success();
  }
}

static void data_read_json_bool(const ReadCtx* ctx, DataReadResult* res) {
  if (UNLIKELY(!data_check_type(ctx, JsonType_Bool, res))) {
    return;
  }
  *mem_as_t(ctx->data, bool) = json_bool(ctx->doc, ctx->val);
  *res                       = result_success();
}

static void data_read_json_string(const ReadCtx* ctx, DataReadResult* res) {
  if (UNLIKELY(!data_check_type(ctx, JsonType_String, res))) {
    return;
  }
  const String jsonStr = json_string(ctx->doc, ctx->val);

  if (UNLIKELY(ctx->meta.flags & DataFlags_NotEmpty && string_is_empty(jsonStr))) {
    *res = result_fail(DataReadError_EmptyStringIsInvalid, "Value cannot be an empty string");
    return;
  }

  const String str = string_is_empty(jsonStr) ? string_empty : string_dup(ctx->alloc, jsonStr);
  data_register_alloc(ctx, str);
  *mem_as_t(ctx->data, String) = str;
  *res                         = result_success();
}

static void data_read_json_struct(const ReadCtx* ctx, DataReadResult* res) {
  if (UNLIKELY(!data_check_type(ctx, JsonType_Object, res))) {
    return;
  }
  const DataDecl* decl = data_decl(ctx->reg, ctx->meta.type);

  mem_set(ctx->data, 0); // Initialize non-specified memory to zero.

  dynarray_for_t(&decl->val_struct.fields, DataDeclField, fieldDecl) {
    const JsonVal fieldVal = json_field(ctx->doc, ctx->val, fieldDecl->id.name);

    if (sentinel_check(fieldVal)) {
      if (fieldDecl->meta.flags & DataFlags_Opt) {
        continue;
      }
      *res = result_fail(
          DataReadError_FieldNotFound, "Field '{}' not found", fmt_text(fieldDecl->id.name));
      return;
    }

    const ReadCtx fieldCtx = {
        .reg         = ctx->reg,
        .alloc       = ctx->alloc,
        .allocations = ctx->allocations,
        .doc         = ctx->doc,
        .val         = fieldVal,
        .meta        = fieldDecl->meta,
        .data        = data_field_mem(ctx->reg, fieldDecl, ctx->data),
    };
    data_read_json_val(&fieldCtx, res);
    if (UNLIKELY(res->error)) {
      *res = result_fail(
          DataReadError_InvalidField,
          "Invalid field '{}': {}",
          fmt_text(fieldDecl->id.name),
          fmt_text(res->errorMsg));
      return;
    }
  }

  *res = result_success();
}

static void data_read_json_enum(const ReadCtx* ctx, DataReadResult* res) {
  if (UNLIKELY(!data_check_type(ctx, JsonType_String, res))) {
    return;
  }
  const DataDecl* decl      = data_decl(ctx->reg, ctx->meta.type);
  const u32       valueHash = bits_hash_32(json_string(ctx->doc, ctx->val));

  dynarray_for_t(&decl->val_enum.consts, DataDeclConst, constDecl) {
    if (constDecl->id.hash == valueHash) {
      *mem_as_t(ctx->data, i32) = constDecl->value;
      *res                      = result_success();
      return;
    }
  }

  *res = result_fail(
      DataReadError_InvalidEnumEntry,
      "Invalid enum entry '{}' for type {}",
      fmt_text(json_string(ctx->doc, ctx->val)),
      fmt_text(decl->id.name));
}

static void data_read_json_val_single(const ReadCtx* ctx, DataReadResult* res) {
  switch (data_decl(ctx->reg, ctx->meta.type)->kind) {
  case DataKind_bool:
    data_read_json_bool(ctx, res);
    return;
  case DataKind_i8:
  case DataKind_i16:
  case DataKind_i32:
  case DataKind_i64:
  case DataKind_u8:
  case DataKind_u16:
  case DataKind_u32:
  case DataKind_u64:
  case DataKind_f32:
  case DataKind_f64:
    data_read_json_number(ctx, res);
    return;
  case DataKind_String:
    data_read_json_string(ctx, res);
    return;
  case DataKind_Struct:
    data_read_json_struct(ctx, res);
    return;
  case DataKind_Enum:
    data_read_json_enum(ctx, res);
    return;
  case DataKind_Invalid:
  case DataKind_Count:
    break;
  }
  diag_crash();
}

static void data_read_json_val_pointer(const ReadCtx* ctx, DataReadResult* res) {
  if (json_type(ctx->doc, ctx->val) == JsonType_Null) {
    if (UNLIKELY(ctx->meta.flags & DataFlags_NotEmpty)) {
      *res = result_fail(DataReadError_NullIsInvalid, "Value cannot be null");
    } else {
      *mem_as_t(ctx->data, void*) = null;
      *res                        = result_success();
    }
    return;
  }

  const DataDecl* decl = data_decl(ctx->reg, ctx->meta.type);
  const Mem       mem  = alloc_alloc(ctx->alloc, decl->size, decl->align);
  data_register_alloc(ctx, mem);

  const ReadCtx subCtx = {
      .reg         = ctx->reg,
      .alloc       = ctx->alloc,
      .allocations = ctx->allocations,
      .doc         = ctx->doc,
      .val         = ctx->val,
      .meta        = data_meta_base(ctx->meta),
      .data        = mem,
  };
  data_read_json_val_single(&subCtx, res);
  *mem_as_t(ctx->data, void*) = mem.ptr;
}

static void data_read_json_val_array(const ReadCtx* ctx, DataReadResult* res) {
  if (UNLIKELY(!data_check_type(ctx, JsonType_Array, res))) {
    return;
  }
  const DataDecl* decl  = data_decl(ctx->reg, ctx->meta.type);
  const usize     count = json_elem_count(ctx->doc, ctx->val);
  if (!count) {
    if (UNLIKELY(ctx->meta.flags & DataFlags_NotEmpty)) {
      *res = result_fail(DataReadError_EmptyArrayIsInvalid, "Value cannot be an empty array");
    } else {
      *mem_as_t(ctx->data, DataArray) = (DataArray){0};
      *res                            = result_success();
    }
    return;
  }

  const Mem arrayMem = alloc_alloc(ctx->alloc, decl->size * count, decl->align);
  data_register_alloc(ctx, arrayMem);

  void* ptr                       = arrayMem.ptr;
  *mem_as_t(ctx->data, DataArray) = (DataArray){.values = arrayMem.ptr, .count = count};

  json_for_elems(ctx->doc, ctx->val, elem) {
    const ReadCtx elemCtx = {
        .reg         = ctx->reg,
        .alloc       = ctx->alloc,
        .allocations = ctx->allocations,
        .doc         = ctx->doc,
        .val         = elem,
        .meta        = data_meta_base(ctx->meta),
        .data        = mem_create(ptr, decl->size),
    };
    data_read_json_val_single(&elemCtx, res);
    if (UNLIKELY(res->error)) {
      return;
    }
    ptr = bits_ptr_offset(ptr, decl->size);
  }

  *res = result_success();
}

static void data_read_json_val(const ReadCtx* ctx, DataReadResult* res) {
  switch (ctx->meta.container) {
  case DataContainer_None:
    data_read_json_val_single(ctx, res);
    return;
  case DataContainer_Pointer:
    data_read_json_val_pointer(ctx, res);
    return;
  case DataContainer_Array:
    data_read_json_val_array(ctx, res);
    return;
  }
  diag_crash();
}

String data_read_json(
    const DataReg*  reg,
    const String    input,
    Allocator*      alloc,
    const DataMeta  meta,
    Mem             data,
    DataReadResult* res) {

  JsonDoc* doc         = json_create(g_alloc_heap, 512);
  DynArray allocations = dynarray_create_t(g_alloc_heap, Mem, 64);

  JsonResult   jsonRes;
  const String rem = json_read(doc, input, &jsonRes);
  if (jsonRes.type != JsonResultType_Success) {
    *res = result_fail(
        DataReadError_Malformed,
        "Json parsing failed: {}",
        fmt_text(json_error_str(jsonRes.error)));
    goto Ret;
  }

  const ReadCtx ctx = {
      .reg         = reg,
      .alloc       = alloc,
      .allocations = &allocations,
      .doc         = doc,
      .val         = jsonRes.val,
      .meta        = meta,
      .data        = data,
  };
  data_read_json_val(&ctx, res);

Ret:
  if (res->error) {
    /**
     * Free all allocations in case of an error.
     * This way the caller doesn't have to attempt to cleanup a half initialized object.
     */
    dynarray_for_t(&allocations, Mem, mem) { alloc_free(alloc, *mem); }
    mem_set(data, 0);
  }
  dynarray_destroy(&allocations);
  json_destroy(doc);
  return rem;
}
