#include "core_alloc.h"
#include "core_annotation.h"
#include "core_diag.h"
#include "core_format.h"
#include "data_reader.h"
#include "data_registry.h"
#include "json_parse.h"

#include "registry_internal.h"

#define result_success()                                                                           \
  (DataReadResult) { 0 }

#define result_fail(_ERR_, _MSG_FORMAT_LIT_, ...)                                                  \
  (DataReadResult) {                                                                               \
    .error = (_ERR_), .message = fmt_write_scratch(_MSG_FORMAT_LIT_, __VA_ARGS__),                 \
  }

static JsonType data_json_type(const DataKind kind) {
  static const JsonType types[DataKind_Count] = {
      [DataKind_bool]   = JsonType_Bool,
      [DataKind_i8]     = JsonType_Number,
      [DataKind_i16]    = JsonType_Number,
      [DataKind_i32]    = JsonType_Number,
      [DataKind_i64]    = JsonType_Number,
      [DataKind_u8]     = JsonType_Number,
      [DataKind_u16]    = JsonType_Number,
      [DataKind_u32]    = JsonType_Number,
      [DataKind_u64]    = JsonType_Number,
      [DataKind_String] = JsonType_String,
      [DataKind_Struct] = JsonType_Object,
      [DataKind_Enum]   = JsonType_String,
  };
  return types[kind];
}

static DataReadResult data_read_json_number(
    Allocator*     alloc,
    const JsonDoc* jsonDoc,
    const JsonVal  jsonVal,
    const DataType dataType,
    void*          data) {

  (void)alloc;

#define READ_PRIM_NUM(_T_)                                                                         \
  case DataKind_##_T_:                                                                             \
    *(_T_*)data = (_T_)json_number(jsonDoc, jsonVal);                                              \
    return result_success()

  switch (data_type_kind(dataType)) {
    READ_PRIM_NUM(i8);
    READ_PRIM_NUM(i16);
    READ_PRIM_NUM(i32);
    READ_PRIM_NUM(i64);
    READ_PRIM_NUM(u8);
    READ_PRIM_NUM(u16);
    READ_PRIM_NUM(u32);
    READ_PRIM_NUM(u64);
    READ_PRIM_NUM(f32);
    READ_PRIM_NUM(f64);
  default:
    diag_crash();
  }

#undef READ_PRIM_NUM
}

static DataReadResult data_read_json_bool(
    Allocator*     alloc,
    const JsonDoc* jsonDoc,
    const JsonVal  jsonVal,
    const DataType dataType,
    void*          data) {

  (void)alloc;
  (void)dataType;

  *(bool*)data = json_bool(jsonDoc, jsonVal);
  return result_success();
}

static DataReadResult data_read_json_string(
    Allocator*     alloc,
    const JsonDoc* jsonDoc,
    const JsonVal  jsonVal,
    const DataType dataType,
    void*          data) {

  (void)dataType;

  *(String*)data = string_dup(alloc, json_string(jsonDoc, jsonVal));
  return result_success();
}

static DataReadResult data_read_json_struct(
    Allocator*     alloc,
    const JsonDoc* jsonDoc,
    const JsonVal  jsonVal,
    const DataType dataType,
    void*          data) {

  (void)alloc;
  (void)jsonDoc;
  (void)jsonVal;
  (void)dataType;
  (void)data;
  return result_success();
}

static DataReadResult data_read_json_enum(
    Allocator*     alloc,
    const JsonDoc* jsonDoc,
    const JsonVal  jsonVal,
    const DataType dataType,
    void*          data) {

  (void)alloc;
  const DataDecl* decl      = data_decl(dataType);
  const u32       valueHash = bits_hash_32(json_string(jsonDoc, jsonVal));

  for (usize i = 0; i != decl->val_enum.count; ++i) {
    const DataDeclConst* constDecl = &decl->val_enum.consts[i];
    if (constDecl->id.hash == valueHash) {
      *(i32*)data = constDecl->value;
      return result_success();
    }
  }
  return result_fail(
      DataReadError_InvalidEnumEntry,
      "Invalid enum entry '{}' for type {}",
      fmt_text(json_string(jsonDoc, jsonVal)),
      fmt_text(decl->id.name));
}

DataReadResult data_read_json(
    Allocator*     alloc,
    const JsonDoc* jsonDoc,
    const JsonVal  jsonVal,
    const DataType dataType,
    void*          data) {

  const DataDecl* decl     = data_decl(dataType);
  const JsonType  jsonType = data_json_type(decl->kind);
  if (UNLIKELY(jsonType != json_type(jsonDoc, jsonVal))) {
    return result_fail(
        DataReadError_MismatchedType,
        "Expected json {} got {}",
        fmt_text(json_type_str(jsonType)),
        fmt_text(json_type_str(json_type(jsonDoc, jsonVal))));
  }

  switch (decl->kind) {
  case DataKind_bool:
    return data_read_json_bool(alloc, jsonDoc, jsonVal, dataType, data);
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
    return data_read_json_number(alloc, jsonDoc, jsonVal, dataType, data);
  case DataKind_String:
    return data_read_json_string(alloc, jsonDoc, jsonVal, dataType, data);
  case DataKind_Struct:
    return data_read_json_struct(alloc, jsonDoc, jsonVal, dataType, data);
  case DataKind_Enum:
    return data_read_json_enum(alloc, jsonDoc, jsonVal, dataType, data);

  case DataKind_Invalid:
  case DataKind_Count:
    break;
  }
  diag_crash();
}

String data_read_json(
    const String    input,
    Allocator*      alloc,
    const DataType  dataType,
    void*           data,
    DataReadResult* res) {
  // TODO: Is it feasible to always store this in scratch memory?
  JsonDoc* doc = json_create(g_alloc_scratch, 256);

  JsonResult   jsonRes;
  const String rem = json_read(doc, input, &jsonRes);
  if (jsonRes.type != JsonResultType_Success) {
    *res = result_fail(
        DataReadError_Malformed,
        "Json parsing failed: {}",
        fmt_text(json_error_str(jsonRes.error)));
    goto Ret;
  }

Ret:
  json_destroy(doc);
  return rem;
}
