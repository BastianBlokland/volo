#include "core_alloc.h"
#include "core_bits.h"
#include "core_diag.h"
#include "core_float.h"
#include "data_schema.h"
#include "json_doc.h"
#include "json_write.h"

#include "registry_internal.h"

#define jsonschema_max_types 512

typedef struct {
  const DataReg* reg;
  JsonDoc*       doc;
  BitSet         addedDefs;
  JsonVal        rootObj, defsObj;
} JsonSchemaCtx;

static void schema_add_type(const JsonSchemaCtx*, JsonVal, DataMeta);

static f64 schema_integer_min(const DataKind kind) {
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

static f64 schema_integer_max(const DataKind kind) {
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

static void schema_add_integer(const JsonSchemaCtx* ctx, const JsonVal obj, const DataMeta meta) {
  const DataDecl* decl = data_decl(ctx->reg, meta.type);

  json_add_field_lit(ctx->doc, obj, "type", json_add_string_lit(ctx->doc, "integer"));

  const f64 min = schema_integer_min(decl->kind);
  if (min == 0.0 && meta.flags & DataFlags_NotEmpty) {
    json_add_field_lit(ctx->doc, obj, "exclusiveMinimum", json_add_number(ctx->doc, min));
  } else {
    json_add_field_lit(ctx->doc, obj, "minimum", json_add_number(ctx->doc, min));
  }

  const f64 max = schema_integer_max(decl->kind);
  json_add_field_lit(ctx->doc, obj, "maximum", json_add_number(ctx->doc, max));
}

static void schema_add_number(const JsonSchemaCtx* ctx, const JsonVal obj, const DataMeta meta) {
  (void)meta;
  json_add_field_lit(ctx->doc, obj, "type", json_add_string_lit(ctx->doc, "number"));
}

static void schema_add_string(const JsonSchemaCtx* ctx, const JsonVal obj, const DataMeta meta) {
  json_add_field_lit(ctx->doc, obj, "type", json_add_string_lit(ctx->doc, "string"));
  if (meta.flags & DataFlags_NotEmpty) {
    json_add_field_lit(ctx->doc, obj, "minLength", json_add_number(ctx->doc, 1));
  }
}

static void schema_add_struct(const JsonSchemaCtx* ctx, const JsonVal obj, const DataMeta meta) {
  const DataDecl* decl = data_decl(ctx->reg, meta.type);
  diag_assert(decl->kind == DataKind_Struct);

  json_add_field_lit(ctx->doc, obj, "type", json_add_string_lit(ctx->doc, "object"));
  json_add_field_lit(ctx->doc, obj, "additionalProperties", json_add_bool(ctx->doc, false));

  const JsonVal propObj = json_add_object(ctx->doc);
  json_add_field_lit(ctx->doc, obj, "properties", propObj);

  const JsonVal reqArr = json_add_array(ctx->doc);
  json_add_field_lit(ctx->doc, obj, "required", reqArr);

  dynarray_for_t(&decl->val_struct.fields, DataDeclField, fieldDecl) {
    const JsonVal fieldObj = json_add_object(ctx->doc);
    json_add_field(ctx->doc, propObj, json_add_string(ctx->doc, fieldDecl->id.name), fieldObj);

    if (!(fieldDecl->meta.flags & DataFlags_Opt)) {
      json_add_elem(ctx->doc, reqArr, json_add_string(ctx->doc, fieldDecl->id.name));
    }

    schema_add_type(ctx, fieldObj, fieldDecl->meta);
  }
}

static void schema_add_union(const JsonSchemaCtx* ctx, const JsonVal obj, const DataMeta meta) {
  const DataDecl* decl = data_decl(ctx->reg, meta.type);
  diag_assert(decl->kind == DataKind_Union);

  const JsonVal anyOfArr = json_add_array(ctx->doc);
  json_add_field_lit(ctx->doc, obj, "anyOf", anyOfArr);

  dynarray_for_t(&decl->val_union.choices, DataDeclChoice, choice) {
    const JsonVal choiceObj = json_add_object(ctx->doc);
    json_add_elem(ctx->doc, anyOfArr, choiceObj);

    json_add_field_lit(ctx->doc, choiceObj, "type", json_add_string_lit(ctx->doc, "object"));
    json_add_field_lit(ctx->doc, choiceObj, "additionalProperties", json_add_bool(ctx->doc, false));

    const JsonVal propObj = json_add_object(ctx->doc);
    json_add_field_lit(ctx->doc, choiceObj, "properties", propObj);

    const JsonVal reqArr = json_add_array(ctx->doc);
    json_add_field_lit(ctx->doc, choiceObj, "required", reqArr);

    const JsonVal typeObj = json_add_object(ctx->doc);
    json_add_field_lit(ctx->doc, propObj, "$type", typeObj);
    json_add_elem(ctx->doc, reqArr, json_add_string_lit(ctx->doc, "$type"));
    json_add_field_lit(ctx->doc, typeObj, "const", json_add_string(ctx->doc, choice->id.name));

    const JsonVal nameObj = json_add_object(ctx->doc);
    json_add_field_lit(ctx->doc, propObj, "$name", nameObj);
    json_add_field_lit(ctx->doc, nameObj, "type", json_add_string_lit(ctx->doc, "string"));

    if (!choice->meta.type) {
      continue; // Empty choice doesn't have any data.
    }
    const DataDecl* choiceDecl = data_decl(ctx->reg, choice->meta.type);
    if (choiceDecl->kind == DataKind_Struct) {
      /**
       * Struct fields are inlined into the current json object.
       */
      diag_assert(choice->meta.container == DataContainer_None);

      dynarray_for_t(&choiceDecl->val_struct.fields, DataDeclField, fieldDecl) {
        const JsonVal fieldObj = json_add_object(ctx->doc);
        json_add_field(ctx->doc, propObj, json_add_string(ctx->doc, fieldDecl->id.name), fieldObj);

        if (!(fieldDecl->meta.flags & DataFlags_Opt)) {
          json_add_elem(ctx->doc, reqArr, json_add_string(ctx->doc, fieldDecl->id.name));
        }

        schema_add_type(ctx, fieldObj, fieldDecl->meta);
      }
    } else {
      /**
       * For other data-kinds the data is stored on a $data property.
       */
      const JsonVal dataObj = json_add_object(ctx->doc);
      json_add_field_lit(ctx->doc, choiceObj, "$data", dataObj);
      json_add_elem(ctx->doc, reqArr, json_add_string_lit(ctx->doc, "$data"));

      schema_add_type(ctx, dataObj, choice->meta);
    }
  }
}

static void schema_add_enum(const JsonSchemaCtx* ctx, const JsonVal obj, const DataMeta meta) {
  const DataDecl* decl = data_decl(ctx->reg, meta.type);

  const JsonVal enumKeysArr = json_add_array(ctx->doc);
  json_add_field_lit(ctx->doc, obj, "enum", enumKeysArr);

  dynarray_for_t(&decl->val_enum.consts, DataDeclConst, constDecl) {
    json_add_elem(ctx->doc, enumKeysArr, json_add_string(ctx->doc, constDecl->id.name));
  }
}

static void schema_add_pointer(const JsonSchemaCtx* ctx, const JsonVal obj, const DataMeta meta) {
  if (meta.flags & DataFlags_NotEmpty) {
    schema_add_type(ctx, obj, data_meta_base(meta));
  } else {
    const DataDecl* decl = data_decl(ctx->reg, meta.type);

    const JsonVal anyOfArr = json_add_array(ctx->doc);
    json_add_field_lit(ctx->doc, obj, "anyOf", anyOfArr);

    const JsonVal someObj = json_add_object(ctx->doc);
    schema_add_type(ctx, someObj, data_meta_base(meta));

    const JsonVal noneObj = json_add_object(ctx->doc);
    json_add_field_lit(ctx->doc, noneObj, "const", json_add_null(ctx->doc));
    json_add_field_lit(ctx->doc, noneObj, "title", json_add_string(ctx->doc, decl->id.name));

    json_add_elem(ctx->doc, anyOfArr, someObj);
    json_add_elem(ctx->doc, anyOfArr, noneObj);
  }
}

static void schema_add_array(const JsonSchemaCtx* ctx, const JsonVal obj, const DataMeta meta) {
  json_add_field_lit(ctx->doc, obj, "type", json_add_string_lit(ctx->doc, "array"));

  if (meta.flags & DataFlags_NotEmpty) {
    json_add_field_lit(ctx->doc, obj, "minItems", json_add_number(ctx->doc, 1));
  }

  const JsonVal itemsObj = json_add_object(ctx->doc);
  json_add_field_lit(ctx->doc, obj, "items", itemsObj);

  schema_add_type(ctx, itemsObj, data_meta_base(meta));
}

static void schema_add_ref(const JsonSchemaCtx* ctx, const JsonVal obj, const DataMeta meta) {
  const DataDecl* decl = data_decl(ctx->reg, meta.type);

  const String defPath = fmt_write_scratch("#/$defs/{}", fmt_text(decl->id.name));
  json_add_field_lit(ctx->doc, obj, "$ref", json_add_string(ctx->doc, defPath));

  if (!bitset_test(ctx->addedDefs, meta.type)) {
    bitset_set(ctx->addedDefs, meta.type);

    const JsonVal defObj = json_add_object(ctx->doc);
    json_add_field_str(ctx->doc, ctx->defsObj, decl->id.name, defObj);

    switch (decl->kind) {
    case DataKind_Struct:
      schema_add_struct(ctx, defObj, meta);
      break;
    case DataKind_Union:
      schema_add_union(ctx, defObj, meta);
      break;
    case DataKind_Enum:
      schema_add_enum(ctx, defObj, meta);
      break;
    default:
      diag_crash_msg("Unsupported json-schema $ref type");
    }
  }
}

static void schema_add_type(const JsonSchemaCtx* ctx, const JsonVal obj, const DataMeta meta) {
  switch (meta.container) {
  case DataContainer_None: {
    const DataDecl* decl = data_decl(ctx->reg, meta.type);

    json_add_field_lit(ctx->doc, obj, "title", json_add_string(ctx->doc, decl->id.name));
    if (!string_is_empty(decl->comment)) {
      json_add_field_lit(ctx->doc, obj, "description", json_add_string(ctx->doc, decl->comment));
    }

    switch (decl->kind) {
    case DataKind_bool:
      json_add_field_lit(ctx->doc, obj, "type", json_add_string_lit(ctx->doc, "boolean"));
      break;
    case DataKind_i8:
    case DataKind_i16:
    case DataKind_i32:
    case DataKind_i64:
    case DataKind_u8:
    case DataKind_u16:
    case DataKind_u32:
    case DataKind_u64:
      schema_add_integer(ctx, obj, meta);
      break;
    case DataKind_f32:
    case DataKind_f64:
      schema_add_number(ctx, obj, meta);
      break;
    case DataKind_String:
      schema_add_string(ctx, obj, meta);
      break;
    case DataKind_Struct:
    case DataKind_Union:
    case DataKind_Enum:
      schema_add_ref(ctx, obj, meta);
      break;
    case DataKind_Invalid:
    case DataKind_Count:
      UNREACHABLE
    }
  } break;
  case DataContainer_Pointer:
    schema_add_pointer(ctx, obj, meta);
    break;
  case DataContainer_Array:
    schema_add_array(ctx, obj, meta);
    break;
  }
}

void data_jsonschema_write(const DataReg* reg, DynString* str, const DataMeta meta) {
  JsonDoc*      doc     = json_create(g_alloc_scratch, 512);
  const JsonVal rootObj = json_add_object(doc);

  const JsonVal defsObj = json_add_object(doc);

  diag_assert(data_type_count(reg) <= jsonschema_max_types);
  u8 addedDefsBits[bits_to_bytes(jsonschema_max_types) + 1] = {0};

  const JsonSchemaCtx ctx = {
      .reg       = reg,
      .doc       = doc,
      .addedDefs = bitset_from_var(addedDefsBits),
      .rootObj   = rootObj,
      .defsObj   = defsObj,
  };
  schema_add_type(&ctx, rootObj, meta);

  if (bitset_any(ctx.addedDefs)) {
    json_add_field_lit(doc, rootObj, "$defs", defsObj);
  }

  json_write(str, doc, rootObj, &json_write_opts());
  json_destroy(doc);
}
