#include "core_alloc.h"
#include "core_bits.h"
#include "core_diag.h"
#include "data_write.h"
#include "json_write.h"

#include "registry_internal.h"

typedef struct {
  JsonDoc*       doc;
  const DataMeta meta;
  Mem            data;
} WriteCtx;

static JsonVal data_write_json_val(const WriteCtx*);

static JsonVal data_write_json_bool(const WriteCtx* ctx) {
  return json_add_bool(ctx->doc, *mem_as_t(ctx->data, bool));
}

static JsonVal data_write_json_number(const WriteCtx* ctx) {
#define RET_ADD_NUM(_T_)                                                                           \
  case DataKind_##_T_:                                                                             \
    return json_add_number(ctx->doc, (f64)*mem_as_t(ctx->data, _T_))

  switch (data_decl(ctx->meta.type)->kind) {
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
  default:
    diag_crash();
  }

#undef RET_ADD_NUM
}

static JsonVal data_write_json_string(const WriteCtx* ctx) {
  return json_add_string(ctx->doc, *mem_as_t(ctx->data, String));
}

static JsonVal data_write_json_struct(const WriteCtx* ctx) {
  const JsonVal   jsonObj = json_add_object(ctx->doc);
  const DataDecl* decl    = data_decl(ctx->meta.type);

  for (usize i = 0; i != decl->val_struct.count; ++i) {
    const DataDeclField* field    = &decl->val_struct.fields[i];
    const WriteCtx       fieldCtx = {
        .doc  = ctx->doc,
        .meta = field->meta,
        .data = data_field_mem(field, ctx->data),
    };
    const JsonVal fieldVal = data_write_json_val(&fieldCtx);
    json_add_field_str(ctx->doc, jsonObj, field->id.name, fieldVal);
  }
  return jsonObj;
}

static JsonVal data_write_json_enum(const WriteCtx* ctx) {
  const i32       val  = *mem_as_t(ctx->data, i32);
  const DataDecl* decl = data_decl(ctx->meta.type);

  for (usize i = 0; i != decl->val_enum.count; ++i) {
    const DataDeclConst* constDecl = &decl->val_enum.consts[i];
    if (constDecl->value == val) {
      return json_add_string(ctx->doc, constDecl->id.name);
    }
  }
  return json_add_null(ctx->doc);
}

static JsonVal data_write_json_val_single(const WriteCtx* ctx) {
  switch (data_decl(ctx->meta.type)->kind) {
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
  case DataKind_f32:
  case DataKind_f64:
    return data_write_json_number(ctx);
  case DataKind_String:
    return data_write_json_string(ctx);
  case DataKind_Struct:
    return data_write_json_struct(ctx);
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
  const DataDecl* decl   = data_decl(ctx->meta.type);
  const WriteCtx  subCtx = {
      .doc  = ctx->doc,
      .meta = {.type = ctx->meta.type},
      .data = mem_create(ptr, decl->size),
  };
  return data_write_json_val(&subCtx);
}

static JsonVal data_write_json_val_array(const WriteCtx* ctx) {
  const JsonVal   jsonArray = json_add_array(ctx->doc);
  const DataDecl* decl      = data_decl(ctx->meta.type);

  const DataArray* array = mem_as_t(ctx->data, DataArray);
  for (usize i = 0; i != array->count; ++i) {
    const WriteCtx elemCtx = {
        .doc  = ctx->doc,
        .meta = {.type = ctx->meta.type},
        .data = mem_create(bits_ptr_offset(array->data, decl->size * i), decl->size),
    };
    const JsonVal elemVal = data_write_json_val(&elemCtx);
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
  case DataContainer_Array:
    return data_write_json_val_array(ctx);
  }
  diag_crash();
}

void data_write_json(DynString* str, const DataMeta meta, const Mem data) {
  JsonDoc*       doc = json_create(g_alloc_scratch, 1024, JsonDocFlags_NoStringDup);
  const WriteCtx ctx = {
      .doc  = doc,
      .meta = meta,
      .data = data,
  };
  const JsonVal val = data_write_json_val(&ctx);

  json_write(str, doc, val, &json_write_opts());
  json_destroy(doc);
}
