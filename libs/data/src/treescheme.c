#include "core_alloc.h"
#include "core_bits.h"
#include "core_diag.h"
#include "data_treescheme.h"
#include "json_doc.h"
#include "json_write.h"

#include "registry_internal.h"

#define treescheme_max_types 512

typedef enum {
  TreeSchemeType_Boolean,
  TreeSchemeType_Number,
  TreeSchemeType_String,
  TreeSchemeType_Alias,
  TreeSchemeType_Enum,
} TreeSchemeType;

typedef struct {
  const DataReg* reg;
  JsonDoc*       doc;
  BitSet         addedTypes;
  JsonVal        schemeObj;
  JsonVal        schemeAliasesArr, schemeEnumsArr, schemeNodesArr;
} TreeSchemeCtx;

static TreeSchemeType treescheme_classify(const TreeSchemeCtx* ctx, const DataType type) {
  switch (data_decl(ctx->reg, type)->kind) {
  case DataKind_bool:
    return TreeSchemeType_Boolean;
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
    return TreeSchemeType_Number;
  case DataKind_String:
    return TreeSchemeType_String;
  case DataKind_Struct:
  case DataKind_Union:
    return TreeSchemeType_Alias;
  case DataKind_Enum:
    return TreeSchemeType_Enum;
  case DataKind_Invalid:
  case DataKind_Count:
    break;
  }
  diag_crash_msg("Unsupported treescheme type");
}

static bool treescheme_check_added(const TreeSchemeCtx* ctx, const DataType type) {
  if (bitset_test(ctx->addedTypes, type)) {
    return true;
  }
  bitset_set(ctx->addedTypes, type);
  return false;
}

static void treescheme_add_enum(const TreeSchemeCtx* ctx, const DataType type) {
  if (treescheme_check_added(ctx, type)) {
    return;
  }
  const DataDecl* decl = data_decl(ctx->reg, type);
  diag_assert(decl->kind == DataKind_Enum);

  const JsonVal enumObj = json_add_object(ctx->doc);
  json_add_elem(ctx->doc, ctx->schemeEnumsArr, enumObj);

  json_add_field_lit(ctx->doc, enumObj, "identifier", json_add_string(ctx->doc, decl->id.name));

  const JsonVal enumValues = json_add_array(ctx->doc);
  json_add_elem(ctx->doc, enumObj, enumValues);

  dynarray_for_t(&decl->val_enum.consts, DataDeclConst, constDecl) {
    const JsonVal valueObj = json_add_object(ctx->doc);
    json_add_elem(ctx->doc, enumValues, valueObj);

    json_add_field_lit(ctx->doc, valueObj, "value", json_add_number(ctx->doc, constDecl->value));
    json_add_field_lit(ctx->doc, valueObj, "name", json_add_string(ctx->doc, constDecl->id.name));
  }
}

static void treescheme_add_alias(const TreeSchemeCtx* ctx, DataType);

static void treescheme_add_node(const TreeSchemeCtx* ctx, const DataType type) {
  if (treescheme_check_added(ctx, type)) {
    return;
  }
  const DataDecl* decl = data_decl(ctx->reg, type);
  diag_assert(decl->kind == DataKind_Struct);

  const JsonVal nodeObj = json_add_object(ctx->doc);
  json_add_elem(ctx->doc, ctx->schemeNodesArr, nodeObj);

  json_add_field_lit(ctx->doc, nodeObj, "nodeType", json_add_string(ctx->doc, decl->id.name));

  const JsonVal nodeFields = json_add_array(ctx->doc);
  json_add_field_lit(ctx->doc, nodeObj, "fields", nodeFields);

  dynarray_for_t(&decl->val_struct.fields, DataDeclField, fieldDecl) {
    const JsonVal fieldObj = json_add_object(ctx->doc);
    json_add_elem(ctx->doc, nodeFields, fieldObj);

    json_add_field_lit(ctx->doc, fieldObj, "name", json_add_string(ctx->doc, fieldDecl->id.name));

    const bool isArray = fieldDecl->meta.container == DataContainer_Array;
    json_add_field_lit(ctx->doc, fieldObj, "isArray", json_add_bool(ctx->doc, isArray));

    const DataDecl* fieldTypeDecl = data_decl(ctx->reg, fieldDecl->meta.type);
    JsonVal         valueType;
    switch (treescheme_classify(ctx, fieldDecl->meta.type)) {
    case TreeSchemeType_Boolean:
      valueType = json_add_string_lit(ctx->doc, "boolean");
      break;
    case TreeSchemeType_Number:
      valueType = json_add_string_lit(ctx->doc, "number");
      break;
    case TreeSchemeType_String:
      valueType = json_add_string_lit(ctx->doc, "string");
      break;
    case TreeSchemeType_Alias:
      valueType = json_add_string(ctx->doc, fieldTypeDecl->id.name);
      treescheme_add_alias(ctx, fieldDecl->meta.type);
      break;
    case TreeSchemeType_Enum:
      valueType = json_add_string(ctx->doc, fieldTypeDecl->id.name);
      treescheme_add_enum(ctx, fieldDecl->meta.type);
      break;
    }
    json_add_field_lit(ctx->doc, fieldObj, "valueType", valueType);
  }
}

static void treescheme_add_node_empty(const TreeSchemeCtx* ctx, const DataId id) {
  // TODO: Verify that there are no other nodes with the same id.
  const JsonVal nodeObj = json_add_object(ctx->doc);
  json_add_elem(ctx->doc, ctx->schemeNodesArr, nodeObj);

  json_add_field_lit(ctx->doc, nodeObj, "nodeType", json_add_string(ctx->doc, id.name));
  json_add_field_lit(ctx->doc, nodeObj, "fields", json_add_array(ctx->doc));
}

static void treescheme_add_alias(const TreeSchemeCtx* ctx, const DataType type) {
  if (treescheme_check_added(ctx, type)) {
    return;
  }
  const DataDecl* decl = data_decl(ctx->reg, type);

  const JsonVal aliasObj = json_add_object(ctx->doc);
  json_add_elem(ctx->doc, ctx->schemeAliasesArr, aliasObj);

  json_add_field_lit(ctx->doc, aliasObj, "identifier", json_add_string(ctx->doc, decl->id.name));

  const JsonVal aliasValues = json_add_array(ctx->doc);
  json_add_field_lit(ctx->doc, aliasObj, "values", aliasValues);

  switch (decl->kind) {
  case DataKind_Struct: {
    // A struct only has a single implementation, so add it as the only value of the alias.
    treescheme_add_node(ctx, type);
    json_add_elem(ctx->doc, aliasValues, json_add_string(ctx->doc, decl->id.name));
  } break;
  case DataKind_Union: {
    // Add all union choices as alias values.
    dynarray_for_t(&decl->val_union.choices, DataDeclChoice, choice) {
      diag_assert(choice->meta.container != DataContainer_Array);
      const bool emptyChoice = choice->meta.type == 0;
      if (emptyChoice) {
        treescheme_add_node_empty(ctx, choice->id);
      } else {
        treescheme_add_node(ctx, choice->meta.type);
      }
      json_add_elem(ctx->doc, aliasValues, json_add_string(ctx->doc, choice->id.name));
    }
  } break;
  default:
    diag_crash_msg("Unsupported treescheme alias type");
  }
}

void data_treescheme_write(const DataReg* reg, DynString* str, const DataType rootType) {
  JsonDoc*      doc              = json_create(g_alloc_scratch, 512);
  const JsonVal schemeAliasesArr = json_add_array(doc);
  const JsonVal schemeEnumsArr   = json_add_array(doc);
  const JsonVal schemeNodesArr   = json_add_array(doc);

  const JsonVal schemeObj = json_add_object(doc);
  json_add_field_lit(doc, schemeObj, "aliases", schemeAliasesArr);
  json_add_field_lit(doc, schemeObj, "enums", schemeEnumsArr);
  json_add_field_lit(doc, schemeObj, "nodes", schemeNodesArr);

  diag_assert(data_type_count(reg) <= treescheme_max_types);
  u8 addedTypesBits[bits_to_bytes(treescheme_max_types) + 1] = {0};

  const TreeSchemeCtx ctx = {
      .reg              = reg,
      .doc              = doc,
      .addedTypes       = bitset_from_var(addedTypesBits),
      .schemeObj        = schemeObj,
      .schemeAliasesArr = schemeAliasesArr,
      .schemeEnumsArr   = schemeEnumsArr,
      .schemeNodesArr   = schemeNodesArr,
  };
  treescheme_add_alias(&ctx, rootType);

  json_add_field_lit(
      doc, schemeObj, "rootAlias", json_add_string(doc, data_decl(reg, rootType)->id.name));

  json_write(str, doc, schemeObj, &json_write_opts());
  json_destroy(doc);
}
