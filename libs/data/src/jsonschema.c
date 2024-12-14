#include "core_alloc.h"
#include "core_base64.h"
#include "core_bits.h"
#include "core_diag.h"
#include "core_dynstring.h"
#include "core_float.h"
#include "data_schema.h"
#include "json_doc.h"
#include "json_write.h"

#include "registry_internal.h"

#define jsonschema_max_types 512
#define jsonschema_snippet_len_max (8 * usize_kibibyte)

typedef struct {
  const DataReg* reg;
  JsonDoc*       doc;
  BitSet         addedDefs;
  JsonVal        rootObj, defsObj;
} JsonSchemaCtx;

static JsonVal schema_default_type(const JsonSchemaCtx*, DataMeta);

static JsonVal schema_default_number(const JsonSchemaCtx* ctx, const DataMeta meta) {
  return json_add_number(ctx->doc, meta.flags & DataFlags_NotEmpty ? 1.0 : 0.0);
}

static JsonVal schema_default_string(const JsonSchemaCtx* ctx, const DataMeta meta) {
  const String str = meta.flags & DataFlags_NotEmpty ? string_lit("placeholder") : string_empty;
  return json_add_string(ctx->doc, str);
}

static JsonVal schema_default_mem(const JsonSchemaCtx* ctx, const DataMeta meta) {
  (void)meta;
  return json_add_string(ctx->doc, string_empty);
}

static JsonVal schema_default_struct(const JsonSchemaCtx* ctx, const DataMeta meta) {
  const DataDecl* decl = data_decl(ctx->reg, meta.type);
  diag_assert(decl->kind == DataKind_Struct);

  const DataDeclField* inlineField = data_struct_inline_field(&decl->val_struct);
  if (inlineField) {
    return schema_default_type(ctx, inlineField->meta);
  }

  const JsonVal obj = json_add_object(ctx->doc);
  dynarray_for_t(&decl->val_struct.fields, DataDeclField, fieldDecl) {
    if (fieldDecl->meta.flags & DataFlags_Opt) {
      continue;
    }
    const JsonVal fieldVal = schema_default_type(ctx, fieldDecl->meta);
    json_add_field_str(ctx->doc, obj, fieldDecl->id.name, fieldVal);
  }
  return obj;
}

static JsonVal schema_default_union_choice(
    const JsonSchemaCtx* ctx, const DataDeclUnion* data, const DataDeclChoice* choice) {
  const JsonVal obj = json_add_object(ctx->doc);

  const JsonVal typeStr = json_add_string(ctx->doc, choice->id.name);
  json_add_field_lit(ctx->doc, obj, "$type", typeStr);

  if (data_union_name_type(data)) {
    const JsonVal defaultName = json_add_string(ctx->doc, string_lit("MyUnion"));
    json_add_field_lit(ctx->doc, obj, "$name", defaultName);
  }

  if (choice->meta.type) {
    const DataDecl* choiceDecl = data_decl(ctx->reg, choice->meta.type);
    if (choiceDecl->kind == DataKind_Struct && !data_struct_inline_field(&choiceDecl->val_struct)) {
      /**
       * Struct fields are inlined into the current json object.
       */
      dynarray_for_t(&choiceDecl->val_struct.fields, DataDeclField, fieldDecl) {
        if (fieldDecl->meta.flags & DataFlags_Opt) {
          continue;
        }
        const JsonVal fieldVal = schema_default_type(ctx, fieldDecl->meta);
        json_add_field_str(ctx->doc, obj, fieldDecl->id.name, fieldVal);
      }
    } else {
      /**
       * For other data-kinds the data is stored on a $data property.
       */
      const JsonVal dataVal = schema_default_type(ctx, choice->meta);
      json_add_field_lit(ctx->doc, obj, "$data", dataVal);
    }
  }

  return obj;
}

static JsonVal schema_default_union(const JsonSchemaCtx* ctx, const DataMeta meta) {
  const DataDecl* decl = data_decl(ctx->reg, meta.type);
  diag_assert(decl->kind == DataKind_Union);

  const DynArray* choices = &decl->val_union.choices;
  if (!dynarray_size(choices)) {
    return json_add_null(ctx->doc);
  }
  const DataDeclChoice* firstChoice = dynarray_at_t(choices, 0, DataDeclChoice);
  return schema_default_union_choice(ctx, &decl->val_union, firstChoice);
}

static JsonVal schema_default_enum(const JsonSchemaCtx* ctx, const DataMeta meta) {
  const DataDecl* decl = data_decl(ctx->reg, meta.type);
  diag_assert(decl->kind == DataKind_Enum);

  const DynArray* consts = &decl->val_enum.consts;

  if (decl->val_enum.multi) {
    const JsonVal arr = json_add_array(ctx->doc);
    if (meta.flags & DataFlags_NotEmpty && dynarray_size(consts)) {
      json_add_elem(
          ctx->doc,
          arr,
          json_add_string(ctx->doc, dynarray_at_t(consts, 0, DataDeclConst)->id.name));
    }
    return arr;
  }

  if (!dynarray_size(consts)) {
    return json_add_null(ctx->doc);
  }
  return json_add_string(ctx->doc, dynarray_at_t(consts, 0, DataDeclConst)->id.name);
}

static JsonVal schema_default_opaque(const JsonSchemaCtx* ctx, const DataMeta meta) {
  const DataDecl* decl = data_decl(ctx->reg, meta.type);
  diag_assert(decl->kind == DataKind_Opaque);

  const Mem zeroMem = alloc_alloc(g_allocScratch, decl->size, 1);
  mem_set(zeroMem, 0);

  return json_add_string(ctx->doc, base64_encode_scratch(zeroMem));
}

static JsonVal schema_default_array(const JsonSchemaCtx* ctx, const DataMeta meta) {
  const JsonVal arr = json_add_array(ctx->doc);
  if (meta.flags & DataFlags_NotEmpty) {
    json_add_elem(ctx->doc, arr, schema_default_type(ctx, data_meta_base(meta)));
  }
  return arr;
}

static JsonVal schema_default_type(const JsonSchemaCtx* ctx, const DataMeta meta) {
  switch (meta.container) {
  case DataContainer_None:
  case DataContainer_Pointer: {
    const DataDecl* decl = data_decl(ctx->reg, meta.type);
    switch (decl->kind) {
    case DataKind_bool:
      return json_add_bool(ctx->doc, false);
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
    case DataKind_TimeDuration:
    case DataKind_Angle:
      return schema_default_number(ctx, meta);
    case DataKind_String:
    case DataKind_StringHash:
      return schema_default_string(ctx, meta);
    case DataKind_DataMem:
      return schema_default_mem(ctx, meta);
    case DataKind_Struct:
      return schema_default_struct(ctx, meta);
    case DataKind_Union:
      return schema_default_union(ctx, meta);
    case DataKind_Enum:
      return schema_default_enum(ctx, meta);
    case DataKind_Opaque:
      return schema_default_opaque(ctx, meta);
    case DataKind_Invalid:
    case DataKind_Count:
      UNREACHABLE
    }
  } break;
  case DataContainer_InlineArray:
    if (UNLIKELY(!meta.fixedCount)) {
      diag_crash_msg("Inline-arrays need at least 1 entry");
    }
    // Fallthrough.
  case DataContainer_HeapArray:
  case DataContainer_DynArray:
    return schema_default_array(ctx, meta);
  }
  UNREACHABLE
}

static String schema_snippet_stringify_scratch(const JsonSchemaCtx* ctx, const JsonVal val) {
  Mem       scratchMem = alloc_alloc(g_allocScratch, jsonschema_snippet_len_max, 1);
  DynString str        = dynstring_create_over(scratchMem);

  /**
   * Prefix with a caret '^' to prevent the VSCode json language server from stringifying it again.
   * https://code.visualstudio.com/Docs/languages/json#_define-snippets-in-json-schemas
   */
  dynstring_append_char(&str, '^');

  /**
   * Escape dollar-signs as those are used for variable replacement in the VSCode snippet syntax.
   * https://code.visualstudio.com/docs/editor/userdefinedsnippets#_variables
   */
  const JsonWriteFlags jFlags = JsonWriteFlags_EscapeDollarSign;
  const JsonWriteMode  jMode  = JsonWriteMode_Verbose;

  json_write(&str, ctx->doc, val, &json_write_opts(.flags = jFlags, .mode = jMode));

  return dynstring_view(&str);
}

static void
schema_snippet_add_default(const JsonSchemaCtx* ctx, const JsonVal obj, const DataMeta meta) {
  const JsonVal snippetsArr = json_add_array(ctx->doc);
  json_add_field_lit(ctx->doc, obj, "defaultSnippets", snippetsArr);

  const JsonVal defaultSnippetObj = json_add_object(ctx->doc);
  json_add_elem(ctx->doc, snippetsArr, defaultSnippetObj);
  json_add_field_lit(ctx->doc, defaultSnippetObj, "label", json_add_string_lit(ctx->doc, "New"));

  const JsonVal defaultVal = schema_default_type(ctx, meta);
  const String  snippetStr = schema_snippet_stringify_scratch(ctx, defaultVal);
  json_add_field_lit(ctx->doc, defaultSnippetObj, "body", json_add_string(ctx->doc, snippetStr));
}

static void
scheme_snippet_add_union(const JsonSchemaCtx* ctx, const JsonVal obj, const DataMeta meta) {
  const DataDecl* decl = data_decl(ctx->reg, meta.type);
  diag_assert(decl->kind == DataKind_Union);

  const JsonVal snippetsArr = json_add_array(ctx->doc);
  json_add_field_lit(ctx->doc, obj, "defaultSnippets", snippetsArr);

  dynarray_for_t(&decl->val_union.choices, DataDeclChoice, choice) {

    const JsonVal choiceSnippetObj = json_add_object(ctx->doc);
    json_add_elem(ctx->doc, snippetsArr, choiceSnippetObj);
    const String labelStr = fmt_write_scratch("New {}", fmt_text(choice->id.name));
    json_add_field_lit(ctx->doc, choiceSnippetObj, "label", json_add_string(ctx->doc, labelStr));

    const JsonVal defaultVal = schema_default_union_choice(ctx, &decl->val_union, choice);
    const String  snippetStr = schema_snippet_stringify_scratch(ctx, defaultVal);
    json_add_field_lit(ctx->doc, choiceSnippetObj, "body", json_add_string(ctx->doc, snippetStr));
  }
}

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

static void schema_add_mem(const JsonSchemaCtx* ctx, const JsonVal obj, const DataMeta meta) {
  (void)meta;
  json_add_field_lit(ctx->doc, obj, "type", json_add_string_lit(ctx->doc, "string"));
  json_add_field_lit(ctx->doc, obj, "contentEncoding", json_add_string_lit(ctx->doc, "base64"));
}

static void schema_add_struct(const JsonSchemaCtx* ctx, const JsonVal obj, const DataMeta meta) {
  const DataDecl* decl = data_decl(ctx->reg, meta.type);
  diag_assert(decl->kind == DataKind_Struct);

  const DataDeclField* inlineField = data_struct_inline_field(&decl->val_struct);
  if (inlineField) {
    schema_add_type(ctx, obj, inlineField->meta);
    return;
  }

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

  schema_snippet_add_default(ctx, obj, meta);
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

    if (data_union_name_type(&decl->val_union)) {
      const JsonVal nameObj = json_add_object(ctx->doc);
      json_add_field_lit(ctx->doc, propObj, "$name", nameObj);
      json_add_field_lit(ctx->doc, nameObj, "type", json_add_string_lit(ctx->doc, "string"));
      json_add_elem(ctx->doc, reqArr, json_add_string(ctx->doc, string_lit("$name")));
    }

    if (!choice->meta.type) {
      continue; // Empty choice doesn't have any data.
    }
    const DataDecl* choiceDecl = data_decl(ctx->reg, choice->meta.type);
    if (choiceDecl->kind == DataKind_Struct && !data_struct_inline_field(&choiceDecl->val_struct)) {
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
      json_add_field_lit(ctx->doc, propObj, "$data", dataObj);
      json_add_elem(ctx->doc, reqArr, json_add_string_lit(ctx->doc, "$data"));

      schema_add_type(ctx, dataObj, choice->meta);
    }
  }

  scheme_snippet_add_union(ctx, obj, meta);
}

static void schema_add_enum(const JsonSchemaCtx* ctx, const JsonVal obj, const DataMeta meta) {
  const DataDecl* decl = data_decl(ctx->reg, meta.type);
  diag_assert(decl->kind == DataKind_Enum);

  const JsonVal enumKeysArr = json_add_array(ctx->doc);

  dynarray_for_t(&decl->val_enum.consts, DataDeclConst, constDecl) {
    json_add_elem(ctx->doc, enumKeysArr, json_add_string(ctx->doc, constDecl->id.name));
  }

  if (decl->val_enum.multi) {
    json_add_field_lit(ctx->doc, obj, "type", json_add_string_lit(ctx->doc, "array"));
    json_add_field_lit(ctx->doc, obj, "uniqueItems", json_add_bool(ctx->doc, true));

    if (meta.flags & DataFlags_NotEmpty) {
      json_add_field_lit(ctx->doc, obj, "minItems", json_add_number(ctx->doc, 1));
    }

    const JsonVal itemsObj = json_add_object(ctx->doc);
    json_add_field_lit(ctx->doc, obj, "items", itemsObj);

    json_add_field_lit(ctx->doc, itemsObj, "enum", enumKeysArr);
  } else {
    json_add_field_lit(ctx->doc, obj, "enum", enumKeysArr);
  }
}

static void schema_add_opaque(const JsonSchemaCtx* ctx, const JsonVal obj, const DataMeta meta) {
  const DataDecl* decl = data_decl(ctx->reg, meta.type);
  diag_assert(decl->kind == DataKind_Opaque);

  const usize stringLen = base64_encoded_size(decl->size);

  json_add_field_lit(ctx->doc, obj, "type", json_add_string_lit(ctx->doc, "string"));
  json_add_field_lit(ctx->doc, obj, "minLength", json_add_number(ctx->doc, stringLen));
  json_add_field_lit(ctx->doc, obj, "maxLength", json_add_number(ctx->doc, stringLen));
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
  if (meta.fixedCount) {
    json_add_field_lit(ctx->doc, obj, "maxItems", json_add_number(ctx->doc, meta.fixedCount));
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
    case DataKind_Opaque:
      schema_add_opaque(ctx, defObj, meta);
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
    case DataKind_f16:
    case DataKind_f32:
    case DataKind_f64:
    case DataKind_TimeDuration:
    case DataKind_Angle:
      schema_add_number(ctx, obj, meta);
      break;
    case DataKind_String:
    case DataKind_StringHash:
      schema_add_string(ctx, obj, meta);
      break;
    case DataKind_DataMem:
      schema_add_mem(ctx, obj, meta);
      break;
    case DataKind_Struct:
    case DataKind_Union:
    case DataKind_Enum:
    case DataKind_Opaque:
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
  case DataContainer_InlineArray:
    if (UNLIKELY(!meta.fixedCount)) {
      diag_crash_msg("Inline-arrays need at least 1 entry");
    }
  // Fallthrough.
  case DataContainer_HeapArray:
  case DataContainer_DynArray:
    schema_add_array(ctx, obj, meta);
    break;
  }
}

void data_jsonschema_write(
    const DataReg* reg, DynString* str, const DataMeta meta, const DataJsonSchemaFlags flags) {
  JsonDoc*      doc     = json_create(g_allocScratch, 512);
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

  JsonWriteMode jMode;
  if (flags & DataJsonSchemaFlags_Compact) {
    jMode = JsonWriteMode_Compact;
  } else {
    jMode = JsonWriteMode_Verbose;
  }

  json_write(str, doc, rootObj, &json_write_opts(.mode = jMode));
  json_destroy(doc);
}
