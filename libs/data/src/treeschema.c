#include "core_alloc.h"
#include "core_bits.h"
#include "core_diag.h"
#include "data_schema.h"
#include "json_doc.h"
#include "json_write.h"

#include "registry_internal.h"

#define treeschema_max_types 512

typedef enum {
  TreeSchemaType_Boolean,
  TreeSchemaType_Number,
  TreeSchemaType_String,
  TreeSchemaType_Alias,
  TreeSchemaType_Enum,
} TreeSchemaType;

typedef struct {
  const DataReg* reg;
  JsonDoc*       doc;
  BitSet         addedTypes;
  JsonVal        schemaObj;
  JsonVal        schemaAliasesArr, schemaEnumsArr, schemaNodesArr;
} TreeSchemaCtx;

static TreeSchemaType treeschema_classify(const TreeSchemaCtx* ctx, const DataType type) {
  switch (data_decl(ctx->reg, type)->kind) {
  case DataKind_bool:
    return TreeSchemaType_Boolean;
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
    return TreeSchemaType_Number;
  case DataKind_String:
    return TreeSchemaType_String;
  case DataKind_Struct:
  case DataKind_Union:
    return TreeSchemaType_Alias;
  case DataKind_Enum:
    return TreeSchemaType_Enum;
  case DataKind_Invalid:
  case DataKind_Count:
    break;
  }
  diag_crash_msg("Unsupported treeschema type");
}

static void treeschema_mark_added(const TreeSchemaCtx* ctx, const DataType type) {
  bitset_set(ctx->addedTypes, type);
}

static bool treeschema_check_added(const TreeSchemaCtx* ctx, const DataType type) {
  return bitset_test(ctx->addedTypes, type);
}

static void treeschema_add_enum(const TreeSchemaCtx* ctx, const DataType type) {
  if (treeschema_check_added(ctx, type)) {
    return;
  }
  treeschema_mark_added(ctx, type);

  const DataDecl* decl = data_decl(ctx->reg, type);
  diag_assert(decl->kind == DataKind_Enum);

  const JsonVal enumObj = json_add_object(ctx->doc);
  json_add_elem(ctx->doc, ctx->schemaEnumsArr, enumObj);

  json_add_field_lit(ctx->doc, enumObj, "identifier", json_add_string(ctx->doc, decl->id.name));

  const JsonVal enumValues = json_add_array(ctx->doc);
  json_add_field_lit(ctx->doc, enumObj, "values", enumValues);

  dynarray_for_t(&decl->val_enum.consts, DataDeclConst, constDecl) {
    const JsonVal valueObj = json_add_object(ctx->doc);
    json_add_elem(ctx->doc, enumValues, valueObj);

    json_add_field_lit(ctx->doc, valueObj, "value", json_add_number(ctx->doc, constDecl->value));
    json_add_field_lit(ctx->doc, valueObj, "name", json_add_string(ctx->doc, constDecl->id.name));
  }
}

static void treeschema_add_alias(const TreeSchemaCtx* ctx, DataType);

static void
treeschema_add_node(const TreeSchemaCtx* ctx, const DataType type, const String typeName) {
  if (treeschema_check_added(ctx, type)) {
    return;
  }
  treeschema_mark_added(ctx, type);

  const DataDecl* decl = data_decl(ctx->reg, type);
  diag_assert(decl->kind == DataKind_Struct);

  const JsonVal nodeObj = json_add_object(ctx->doc);
  json_add_elem(ctx->doc, ctx->schemaNodesArr, nodeObj);

  json_add_field_lit(ctx->doc, nodeObj, "nodeType", json_add_string(ctx->doc, typeName));

  const String comment = data_comment(ctx->reg, type);
  if (!string_is_empty(comment)) {
    json_add_field_lit(ctx->doc, nodeObj, "comment", json_add_string(ctx->doc, comment));
  }

  const JsonVal nodeFields = json_add_array(ctx->doc);
  json_add_field_lit(ctx->doc, nodeObj, "fields", nodeFields);

  dynarray_for_t(&decl->val_struct.fields, DataDeclField, fieldDecl) {
    const JsonVal fieldObj = json_add_object(ctx->doc);
    json_add_elem(ctx->doc, nodeFields, fieldObj);

    json_add_field_lit(ctx->doc, fieldObj, "name", json_add_string(ctx->doc, fieldDecl->id.name));

    if (fieldDecl->meta.container == DataContainer_Array) {
      json_add_field_lit(ctx->doc, fieldObj, "isArray", json_add_bool(ctx->doc, true));
    }
    if (fieldDecl->meta.flags & DataFlags_HideName) {
      json_add_field_lit(ctx->doc, fieldObj, "hideName", json_add_bool(ctx->doc, true));
    }

    const DataDecl* fieldTypeDecl = data_decl(ctx->reg, fieldDecl->meta.type);
    JsonVal         valueType;
    switch (treeschema_classify(ctx, fieldDecl->meta.type)) {
    case TreeSchemaType_Boolean:
      valueType = json_add_string_lit(ctx->doc, "boolean");
      break;
    case TreeSchemaType_Number:
      valueType = json_add_string_lit(ctx->doc, "number");
      break;
    case TreeSchemaType_String:
      valueType = json_add_string_lit(ctx->doc, "string");
      break;
    case TreeSchemaType_Alias:
      valueType = json_add_string(ctx->doc, fieldTypeDecl->id.name);
      treeschema_add_alias(ctx, fieldDecl->meta.type);
      break;
    case TreeSchemaType_Enum:
      valueType = json_add_string(ctx->doc, fieldTypeDecl->id.name);
      treeschema_add_enum(ctx, fieldDecl->meta.type);
      break;
    }
    json_add_field_lit(ctx->doc, fieldObj, "valueType", valueType);
  }
}

static void treeschema_add_node_empty(const TreeSchemaCtx* ctx, const DataId id) {
  // TODO: Verify that there are no other nodes with the same id.
  const JsonVal nodeObj = json_add_object(ctx->doc);
  json_add_elem(ctx->doc, ctx->schemaNodesArr, nodeObj);

  json_add_field_lit(ctx->doc, nodeObj, "nodeType", json_add_string(ctx->doc, id.name));
  json_add_field_lit(ctx->doc, nodeObj, "fields", json_add_array(ctx->doc));
}

static void treeschema_add_alias(const TreeSchemaCtx* ctx, const DataType type) {
  if (treeschema_check_added(ctx, type)) {
    return;
  }
  const DataDecl* decl = data_decl(ctx->reg, type);
  if (decl->kind != DataKind_Struct) {
    /**
     * Structs are added as aliases which redirect to a single node implementation. Because we use
     * the same data-type for both the alias and the node we only mark it after adding the node.
     */
    treeschema_mark_added(ctx, type);
  }

  const JsonVal aliasObj = json_add_object(ctx->doc);
  json_add_elem(ctx->doc, ctx->schemaAliasesArr, aliasObj);

  json_add_field_lit(ctx->doc, aliasObj, "identifier", json_add_string(ctx->doc, decl->id.name));

  const JsonVal aliasValues = json_add_array(ctx->doc);
  json_add_field_lit(ctx->doc, aliasObj, "values", aliasValues);

  switch (decl->kind) {
  case DataKind_Struct: {
    // A struct only has a single implementation, so add it as the only value of the alias.
    treeschema_add_node(ctx, type, decl->id.name);
    json_add_elem(ctx->doc, aliasValues, json_add_string(ctx->doc, decl->id.name));
  } break;
  case DataKind_Union: {
    // Add all union choices as alias values.
    dynarray_for_t(&decl->val_union.choices, DataDeclChoice, choice) {
      diag_assert(choice->meta.container != DataContainer_Array);
      const bool emptyChoice = choice->meta.type == 0;
      if (emptyChoice) {
        treeschema_add_node_empty(ctx, choice->id);
      } else {
        treeschema_add_node(ctx, choice->meta.type, choice->id.name);
      }
      json_add_elem(ctx->doc, aliasValues, json_add_string(ctx->doc, choice->id.name));
    }
  } break;
  default:
    diag_crash_msg("Unsupported treeschema alias type");
  }
}

void data_treeschema_write(const DataReg* reg, DynString* str, const DataMeta rootMeta) {
  JsonDoc*      doc              = json_create(g_alloc_scratch, 512);
  const JsonVal schemaAliasesArr = json_add_array(doc);
  const JsonVal schemaEnumsArr   = json_add_array(doc);
  const JsonVal schemaNodesArr   = json_add_array(doc);

  const JsonVal schemaObj = json_add_object(doc);
  json_add_field_lit(doc, schemaObj, "aliases", schemaAliasesArr);
  json_add_field_lit(doc, schemaObj, "enums", schemaEnumsArr);
  json_add_field_lit(doc, schemaObj, "nodes", schemaNodesArr);

  diag_assert(data_type_count(reg) <= treeschema_max_types);
  u8 addedTypesBits[bits_to_bytes(treeschema_max_types) + 1] = {0};

  const TreeSchemaCtx ctx = {
      .reg              = reg,
      .doc              = doc,
      .addedTypes       = bitset_from_var(addedTypesBits),
      .schemaObj        = schemaObj,
      .schemaAliasesArr = schemaAliasesArr,
      .schemaEnumsArr   = schemaEnumsArr,
      .schemaNodesArr   = schemaNodesArr,
  };
  treeschema_add_alias(&ctx, rootMeta.type);

  json_add_field_lit(
      doc, schemaObj, "rootAlias", json_add_string(doc, data_decl(reg, rootMeta.type)->id.name));

  json_add_field_lit(doc, schemaObj, "featureNodeNames", json_add_bool(doc, true));

  json_write(str, doc, schemaObj, &json_write_opts());
  json_destroy(doc);
}
