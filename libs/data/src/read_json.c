#include "core_alloc.h"
#include "core_annotation.h"
#include "core_base64.h"
#include "core_bits.h"
#include "core_diag.h"
#include "core_float.h"
#include "core_math.h"
#include "core_stringtable.h"
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

static const DataDeclField* data_field_by_name(const DataDeclStruct* data, const StringHash name) {
  dynarray_for_t(&data->fields, DataDeclField, fieldDecl) {
    if (fieldDecl->id.hash == name) {
      return fieldDecl;
    }
  }
  return null;
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
      [DataKind_f16] = -65504.0,
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
      [DataKind_f16] = 65504.0,
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
    case DataKind_f16:
      *mem_as_t(ctx->data, f16) = float_f32_to_f16((f32)number);
      break;
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

  if (string_is_empty(jsonStr)) {
    *mem_as_t(ctx->data, String) = string_empty;
  } else {
    if (ctx->meta.flags & DataFlags_Intern) {
      *mem_as_t(ctx->data, String) = stringtable_intern(g_stringtable, jsonStr);
    } else {
      const String str = string_dup(ctx->alloc, jsonStr);
      data_register_alloc(ctx, str);
      *mem_as_t(ctx->data, String) = str;
    }
  }
  *res = result_success();
}

static void data_read_json_string_hash(const ReadCtx* ctx, DataReadResult* res) {
  const JsonType valType = json_type(ctx->doc, ctx->val);
  switch (valType) {
  case JsonType_String: {
    const String jsonStr = json_string(ctx->doc, ctx->val);

    if (UNLIKELY(ctx->meta.flags & DataFlags_NotEmpty && string_is_empty(jsonStr))) {
      *res = result_fail(DataReadError_EmptyStringIsInvalid, "Value cannot be an empty string");
      return;
    }

    if (string_is_empty(jsonStr)) {
      *mem_as_t(ctx->data, StringHash) = 0;
    } else {
      *mem_as_t(ctx->data, StringHash) = stringtable_add(g_stringtable, jsonStr);
    }
    *res = result_success();
  } break;
  case JsonType_Number: {
    const u32 jsonNum = (u32)json_number(ctx->doc, ctx->val);

    if (UNLIKELY(ctx->meta.flags & DataFlags_NotEmpty && !jsonNum)) {
      *res = result_fail(DataReadError_ZeroIsInvalid, "Value cannot be zero");
      return;
    }

    *mem_as_t(ctx->data, StringHash) = jsonNum;
    *res                             = result_success();
  } break;
  default:
    *res = result_fail(
        DataReadError_MismatchedType,
        "Expected json string or number got {}",
        fmt_text(json_type_str(valType)));
    break;
  }
}

static usize data_read_json_mem_align(const usize size) {
  const usize biggestPow2 = u64_lit(1) << bits_ctz(size);
  return math_min(biggestPow2, data_type_mem_align_max);
}

static void data_read_json_mem(const ReadCtx* ctx, DataReadResult* res) {
  if (UNLIKELY(!data_check_type(ctx, JsonType_String, res))) {
    return;
  }
  const String jsonStr = json_string(ctx->doc, ctx->val);

  if (UNLIKELY(ctx->meta.flags & DataFlags_NotEmpty && string_is_empty(jsonStr))) {
    *res = result_fail(DataReadError_EmptyStringIsInvalid, "Value cannot be an empty string");
    return;
  }

  const usize decodedSize = base64_decoded_size(jsonStr);
  if (!decodedSize) {
    *mem_as_t(ctx->data, DataMem) = data_mem_create(mem_empty);
    *res                          = result_success();
    return;
  }

  const Mem mem = alloc_alloc(ctx->alloc, decodedSize, data_read_json_mem_align(decodedSize));

  data_register_alloc(ctx, mem);

  DynString memStr = dynstring_create_over(mem);

  if (base64_decode(&memStr, jsonStr)) {
    *mem_as_t(ctx->data, DataMem) = data_mem_create(mem);
    *res                          = result_success();
  } else {
    *res = result_fail(DataReadError_Base64DataInvalid, "Value contains invalid base64 data");
  }
}

static void data_read_json_struct(const ReadCtx* ctx, DataReadResult* res, u32 fieldsRead) {
  if (UNLIKELY(!data_check_type(ctx, JsonType_Object, res))) {
    return;
  }
  const DataDecl* decl = data_decl(ctx->reg, ctx->meta.type);

  /**
   * Initialize non-specified memory to zero.
   * NOTE: We cannot skip this even for structs without holes as fields can be optional.
   */
  mem_set(ctx->data, 0);

  dynarray_for_t(&decl->val_struct.fields, DataDeclField, fieldDecl) {
    const JsonVal fieldVal = json_field(ctx->doc, ctx->val, fieldDecl->id.hash);

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
    ++fieldsRead;
  }

  if (UNLIKELY(fieldsRead != json_field_count(ctx->doc, ctx->val))) {
    json_for_fields(ctx->doc, ctx->val, field) {
      const StringHash nameHash = json_string_hash(ctx->doc, field.name);
      if (!data_field_by_name(&decl->val_struct, nameHash)) {
        String name = json_string(ctx->doc, field.name);
        if (string_is_empty(name)) {
          // Field uses a hash-only name; attempt to retrieve the name from the global string-table.
          name = stringtable_lookup(g_stringtable, nameHash);
        }
        if (string_is_empty(name)) {
          *res = result_fail(DataReadError_UnknownField, "Unknown field: '{}'", fmt_int(nameHash));
        } else {
          *res = result_fail(DataReadError_UnknownField, "Unknown field: '{}'", fmt_text(name));
        }
        return;
      }
    }
    diag_assert_fail("Invalid state");
  }

  *res = result_success();
}

static const DataDeclChoice* data_read_json_union_choice(const ReadCtx* ctx, DataReadResult* res) {
  const DataDecl* decl    = data_decl(ctx->reg, ctx->meta.type);
  const JsonVal   typeVal = json_field_lit(ctx->doc, ctx->val, "$type");

  if (UNLIKELY(sentinel_check(typeVal))) {
    *res = result_fail(DataReadError_UnionTypeMissing, "Union is missing a '$type' field");
    return null;
  }
  if (UNLIKELY(json_type(ctx->doc, typeVal) != JsonType_String)) {
    *res = result_fail(DataReadError_UnionTypeInvalid, "Union '$type' field is invalid");
    return null;
  }

  const StringHash valueHash = json_string_hash(ctx->doc, typeVal);
  dynarray_for_t(&decl->val_union.choices, DataDeclChoice, choice) {
    if (choice->id.hash == valueHash) {
      *res = result_success();
      return choice;
    }
  }

  *res = result_fail(
      DataReadError_UnionTypeUnsupported,
      "Invalid union type '{}' for union {}",
      fmt_text(json_string(ctx->doc, typeVal)),
      fmt_text(decl->id.name));
  return null;
}

static void data_read_json_union(const ReadCtx* ctx, DataReadResult* res) {
  if (UNLIKELY(!data_check_type(ctx, JsonType_Object, res))) {
    return;
  }
  const DataDecl*       decl   = data_decl(ctx->reg, ctx->meta.type);
  const DataDeclChoice* choice = data_read_json_union_choice(ctx, res);
  if (UNLIKELY(res->error)) {
    return;
  }

  mem_set(ctx->data, 0); // Initialize non-specified memory to zero.

  *data_union_tag(&decl->val_union, ctx->data) = choice->tag;

  const JsonVal nameVal = json_field_lit(ctx->doc, ctx->val, "$name");
  if (!sentinel_check(nameVal)) {
    if (UNLIKELY(json_type(ctx->doc, nameVal) != JsonType_String)) {
      *res = result_fail(DataReadError_UnionInvalidName, "'$name' field has to be a string");
      return;
    }
    String* unionNamePtr = data_union_name(&decl->val_union, ctx->data);
    if (UNLIKELY(!unionNamePtr)) {
      *res = result_fail(DataReadError_UnionNameNotSupported, "'$name' field unsupported");
      return;
    }
    const String jsonName = json_string(ctx->doc, nameVal);
    if (!string_is_empty(jsonName)) {
      const String name = string_dup(ctx->alloc, jsonName);
      data_register_alloc(ctx, name);
      *unionNamePtr = name;
    }
  }

  const bool emptyChoice = choice->meta.type == 0;
  if (!emptyChoice) {
    switch (data_decl(ctx->reg, choice->meta.type)->kind) {
    case DataKind_Struct: {
      /**
       * Struct fields are inlined into the current json object.
       */
      const ReadCtx choiceCtx = {
          .reg         = ctx->reg,
          .alloc       = ctx->alloc,
          .allocations = ctx->allocations,
          .doc         = ctx->doc,
          .val         = ctx->val,
          .meta        = choice->meta,
          .data        = data_choice_mem(ctx->reg, choice, ctx->data),
      };
      const u32 fieldsRead = sentinel_check(nameVal) ? 1 : 2;
      data_read_json_struct(&choiceCtx, res, fieldsRead);
    } break;
    default: {
      const JsonVal dataVal = json_field_lit(ctx->doc, ctx->val, "$data");
      if (UNLIKELY(sentinel_check(dataVal))) {
        *res = result_fail(DataReadError_UnionDataMissing, "Union is missing a '$data' field");
        return;
      }
      const ReadCtx choiceCtx = {
          .reg         = ctx->reg,
          .alloc       = ctx->alloc,
          .allocations = ctx->allocations,
          .doc         = ctx->doc,
          .val         = dataVal,
          .meta        = choice->meta,
          .data        = data_choice_mem(ctx->reg, choice, ctx->data),
      };
      data_read_json_val(&choiceCtx, res);
      if (UNLIKELY(res->error)) {
        *res = result_fail(
            DataReadError_UnionDataInvalid,
            "Invalid union data '{}': {}",
            fmt_text(choice->id.name),
            fmt_text(res->errorMsg));
        return;
      }
      const u32 expectedFieldCount = sentinel_check(nameVal) ? 2 : 3;
      if (UNLIKELY(json_field_count(ctx->doc, ctx->val) != expectedFieldCount)) {
        *res = result_fail(DataReadError_UnionUnknownField, "Unknown field in union");
        return;
      }
    } break;
    }
  }
}

static void data_read_json_enum_single_string(const ReadCtx* ctx, DataReadResult* res) {
  const DataDecl*  decl      = data_decl(ctx->reg, ctx->meta.type);
  const StringHash valueHash = json_string_hash(ctx->doc, ctx->val);

  const DataDeclConst* constDecl = data_const_from_id(&decl->val_enum, valueHash);
  if (constDecl) {
    *mem_as_t(ctx->data, i32) = constDecl->value;
    *res                      = result_success();
    return;
  }

  *res = result_fail(
      DataReadError_InvalidEnumEntry,
      "Invalid enum entry '{}' for type {}",
      fmt_text(json_string(ctx->doc, ctx->val)),
      fmt_text(decl->id.name));
}

static void data_read_json_enum_single_number(const ReadCtx* ctx, DataReadResult* res) {
  const DataDecl* decl  = data_decl(ctx->reg, ctx->meta.type);
  const i32       value = (i32)json_number(ctx->doc, ctx->val);

  const DataDeclConst* constDecl = data_const_from_val(&decl->val_enum, value);
  if (constDecl) {
    *mem_as_t(ctx->data, i32) = constDecl->value;
    *res                      = result_success();
    return;
  }

  *res = result_fail(
      DataReadError_InvalidEnumEntry,
      "Invalid enum entry '{}' for type {}",
      fmt_float(json_number(ctx->doc, ctx->val)),
      fmt_text(decl->id.name));
}

static void data_read_json_enum_multi_array(const ReadCtx* ctx, DataReadResult* res) {
  const DataDecl* decl = data_decl(ctx->reg, ctx->meta.type);

  i32 val = 0;
  json_for_elems(ctx->doc, ctx->val, elem) {
    const JsonType elemType = json_type(ctx->doc, elem);
    switch (elemType) {
    case JsonType_String: {
      const StringHash     elemId    = json_string_hash(ctx->doc, elem);
      const DataDeclConst* constDecl = data_const_from_id(&decl->val_enum, elemId);
      if (LIKELY(constDecl)) {
        if (UNLIKELY(val & constDecl->value)) {
          *res = result_fail(
              DataReadError_DuplicateEnumEntry,
              "Duplicate enum entry '{}' for type {}",
              fmt_text(constDecl->id.name),
              fmt_text(decl->id.name));
          return;
        }
        val |= constDecl->value;
      } else {
        *res = result_fail(
            DataReadError_InvalidEnumEntry,
            "Invalid enum entry '{}' for type {}",
            fmt_text(json_string(ctx->doc, elem)),
            fmt_text(decl->id.name));
        return;
      }
    } break;
    case JsonType_Number: {
      const i32            elemVal   = (i32)json_number(ctx->doc, elem);
      const DataDeclConst* constDecl = data_const_from_val(&decl->val_enum, 1 << elemVal);
      if (LIKELY(constDecl)) {
        if (UNLIKELY(val & constDecl->value)) {
          *res = result_fail(
              DataReadError_DuplicateEnumEntry,
              "Duplicate enum entry '{}' for type {}",
              fmt_int(elemVal),
              fmt_text(decl->id.name));
          return;
        }
        val |= constDecl->value;
      } else {
        *res = result_fail(
            DataReadError_InvalidEnumEntry,
            "Invalid enum entry '{}' for type {}",
            fmt_int(elemVal),
            fmt_text(decl->id.name));
        return;
      }
    } break;
    default:
      *res = result_fail(
          DataReadError_MismatchedType,
          "Expected json string or number got {}",
          fmt_text(json_type_str(elemType)));
      return;
    }
  }

  if (UNLIKELY(ctx->meta.flags & DataFlags_NotEmpty && val == 0)) {
    *res = result_fail(DataReadError_EmptyArrayIsInvalid, "At least one value needs to be set");
    return;
  }

  *mem_as_t(ctx->data, i32) = val;
  *res                      = result_success();
}

static void data_read_json_enum(const ReadCtx* ctx, DataReadResult* res) {
  const DataDecl* decl    = data_decl(ctx->reg, ctx->meta.type);
  const JsonType  valType = json_type(ctx->doc, ctx->val);

  if (decl->val_enum.multi) {
    if (LIKELY(valType == JsonType_Array)) {
      data_read_json_enum_multi_array(ctx, res);
    } else {
      *res = result_fail(
          DataReadError_MismatchedType,
          "Expected json array got {}",
          fmt_text(json_type_str(valType)));
    }
    return;
  }

  switch (valType) {
  case JsonType_String:
    data_read_json_enum_single_string(ctx, res);
    break;
  case JsonType_Number:
    data_read_json_enum_single_number(ctx, res);
    break;
  default:
    *res = result_fail(
        DataReadError_MismatchedType,
        "Expected json string or number got {}",
        fmt_text(json_type_str(valType)));
    break;
  }
}

static void data_read_json_opaque(const ReadCtx* ctx, DataReadResult* res) {
  if (UNLIKELY(!data_check_type(ctx, JsonType_String, res))) {
    return;
  }
  const String jsonStr     = json_string(ctx->doc, ctx->val);
  const usize  decodedSize = base64_decoded_size(jsonStr);

  if (UNLIKELY(decodedSize != ctx->data.size)) {
    *res = result_fail(DataReadError_Base64DataInvalid, "Value contains invalid base64 data");
    return;
  }

  DynString memStr = dynstring_create_over(ctx->data);
  if (base64_decode(&memStr, jsonStr)) {
    *res = result_success();
  } else {
    *res = result_fail(DataReadError_Base64DataInvalid, "Value contains invalid base64 data");
  }
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
  case DataKind_f16:
  case DataKind_f32:
  case DataKind_f64:
    data_read_json_number(ctx, res);
    return;
  case DataKind_String:
    data_read_json_string(ctx, res);
    return;
  case DataKind_StringHash:
    data_read_json_string_hash(ctx, res);
    return;
  case DataKind_DataMem:
    data_read_json_mem(ctx, res);
    return;
  case DataKind_Struct: {
    const u32 fieldsRead = 0;
    data_read_json_struct(ctx, res, fieldsRead);
    return;
  }
  case DataKind_Union:
    data_read_json_union(ctx, res);
    return;
  case DataKind_Enum:
    data_read_json_enum(ctx, res);
    return;
  case DataKind_Opaque:
    data_read_json_opaque(ctx, res);
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

static void data_read_json_val_elems(const ReadCtx* ctx, void* out, DataReadResult* res) {
  const DataDecl* decl = data_decl(ctx->reg, ctx->meta.type);

  json_for_elems(ctx->doc, ctx->val, elem) {
    const ReadCtx elemCtx = {
        .reg         = ctx->reg,
        .alloc       = ctx->alloc,
        .allocations = ctx->allocations,
        .doc         = ctx->doc,
        .val         = elem,
        .meta        = data_meta_base(ctx->meta),
        .data        = mem_create(out, decl->size),
    };
    data_read_json_val_single(&elemCtx, res);
    if (UNLIKELY(res->error)) {
      return;
    }
    out = bits_ptr_offset(out, decl->size);
  }

  *res = result_success();
}

static void data_read_json_val_inline_array(const ReadCtx* ctx, DataReadResult* res) {
  if (UNLIKELY(!ctx->meta.fixedCount)) {
    diag_crash_msg("Inline-arrays need at least 1 entry");
  }
  if (UNLIKELY(ctx->data.size != data_meta_size(ctx->reg, ctx->meta))) {
    diag_crash_msg("Unexpected data-size for inline array");
  }
  if (UNLIKELY(!data_check_type(ctx, JsonType_Array, res))) {
    return;
  }
  const usize count = json_elem_count(ctx->doc, ctx->val);
  if (UNLIKELY(count != ctx->meta.fixedCount)) {
    *res = result_fail(
        DataReadError_MismatchedInlineArrayCount,
        "Inline-array expects {} entries, got: {}",
        fmt_int(ctx->meta.fixedCount),
        fmt_int(count));
    return;
  }
  data_read_json_val_elems(ctx, ctx->data.ptr, res);
}

static void data_read_json_val_heap_array(const ReadCtx* ctx, DataReadResult* res) {
  if (UNLIKELY(!data_check_type(ctx, JsonType_Array, res))) {
    return;
  }
  const DataDecl* decl  = data_decl(ctx->reg, ctx->meta.type);
  const usize     count = json_elem_count(ctx->doc, ctx->val);
  if (!count) {
    if (UNLIKELY(ctx->meta.flags & DataFlags_NotEmpty)) {
      *res = result_fail(DataReadError_EmptyArrayIsInvalid, "Value cannot be an empty array");
    } else {
      *mem_as_t(ctx->data, HeapArray) = (HeapArray){0};
      *res                            = result_success();
    }
    return;
  }

  const Mem arrayMem = alloc_alloc(ctx->alloc, decl->size * count, decl->align);
  data_register_alloc(ctx, arrayMem);

  void* ptr                       = arrayMem.ptr;
  *mem_as_t(ctx->data, HeapArray) = (HeapArray){.values = arrayMem.ptr, .count = count};

  data_read_json_val_elems(ctx, ptr, res);
}

static void data_read_json_val_dynarray(const ReadCtx* ctx, DataReadResult* res) {
  if (UNLIKELY(!data_check_type(ctx, JsonType_Array, res))) {
    return;
  }
  const DataDecl* decl = data_decl(ctx->reg, ctx->meta.type);

  DynArray* out = mem_as_t(ctx->data, DynArray);
  *out          = dynarray_create(ctx->alloc, (u32)decl->size, (u16)decl->align, 0);

  const usize count = json_elem_count(ctx->doc, ctx->val);
  if (!count) {
    if (UNLIKELY(ctx->meta.flags & DataFlags_NotEmpty)) {
      *res = result_fail(DataReadError_EmptyArrayIsInvalid, "Value cannot be an empty array");
    } else {
      *res = result_success();
    }
    return;
  }

  dynarray_resize(out, count);
  data_register_alloc(ctx, out->data);

  data_read_json_val_elems(ctx, out->data.ptr, res);
}

static void data_read_json_val(const ReadCtx* ctx, DataReadResult* res) {
  switch (ctx->meta.container) {
  case DataContainer_None:
    data_read_json_val_single(ctx, res);
    return;
  case DataContainer_Pointer:
    data_read_json_val_pointer(ctx, res);
    return;
  case DataContainer_InlineArray:
    data_read_json_val_inline_array(ctx, res);
    return;
  case DataContainer_HeapArray:
    data_read_json_val_heap_array(ctx, res);
    return;
  case DataContainer_DynArray:
    data_read_json_val_dynarray(ctx, res);
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

  JsonDoc* doc         = json_create(g_allocHeap, 512);
  DynArray allocations = dynarray_create_t(g_allocHeap, Mem, 64);

  JsonResult   jsonRes;
  const String rem = json_read(doc, input, JsonReadFlags_HashOnlyFieldNames, &jsonRes);
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
