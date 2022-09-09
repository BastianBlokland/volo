#include "core_alloc.h"
#include "core_bits.h"
#include "core_diag.h"
#include "data_registry.h"

#include "registry_internal.h"

struct sDataReg {
  DynArray   types; // DataDecl[]
  Allocator* alloc;
};

static DataType data_alloc_type(DataReg* reg) {
  dynarray_push(&reg->types, 1);
  return (DataType)reg->types.size;
}

static DataId data_id_create(Allocator* alloc, const String name) {
  return (DataId){.name = string_dup(alloc, name), .hash = string_hash(name)};
}

static void data_id_destroy(Allocator* alloc, DataId id) { string_free(alloc, id.name); }

static DataDecl* data_decl_mutable(DataReg* reg, const DataType type) {
  diag_assert_msg(type, "Uninitialized data-type");
  return dynarray_at_t(&reg->types, type - 1, DataDecl);
}

MAYBE_UNUSED static DataType data_type_by_id(const DataReg* reg, const DataId id) {
  for (DataType declId = 0; declId != reg->types.size; ++declId) {
    DataDecl* decl = dynarray_at_t(&reg->types, declId, DataDecl);
    if (decl->id.hash == id.hash) {
      return declId;
    }
  }
  return sentinel_u32;
}

DataReg* data_reg_create(Allocator* alloc) {
  DataReg* reg = alloc_alloc_t(alloc, DataReg);
  *reg         = (DataReg){
      .types = dynarray_create_t(alloc, DataDecl, 64),
      .alloc = alloc,
  };

#define X(_T_)                                                                                     \
  *data_decl_mutable(reg, data_alloc_type(reg)) = (DataDecl){                                      \
      .kind  = DataKind_##_T_,                                                                     \
      .size  = sizeof(_T_),                                                                        \
      .align = alignof(_T_),                                                                       \
      .id    = data_id_create(alloc, string_lit(#_T_)),                                            \
  };
  DATA_PRIMS
#undef X

  return reg;
}

void data_reg_destroy(DataReg* reg) {

  dynarray_for_t(&reg->types, DataDecl, decl) {
    data_id_destroy(reg->alloc, decl->id);
    switch (decl->kind) {
    case DataKind_Struct: {
      dynarray_for_t(&decl->val_struct.fields, DataDeclField, fieldDecl) {
        data_id_destroy(reg->alloc, fieldDecl->id);
      }
      dynarray_destroy(&decl->val_struct.fields);
    } break;
    case DataKind_Union: {
      dynarray_for_t(&decl->val_union.choices, DataDeclChoice, choiceDecl) {
        data_id_destroy(reg->alloc, choiceDecl->id);
      }
      dynarray_destroy(&decl->val_union.choices);
    } break;
    case DataKind_Enum: {
      dynarray_for_t(&decl->val_enum.consts, DataDeclConst, constDecl) {
        data_id_destroy(reg->alloc, constDecl->id);
      }
      dynarray_destroy(&decl->val_enum.consts);
    } break;
    default:
      break;
    }
  }
  dynarray_destroy(&reg->types);

  alloc_free_t(reg->alloc, reg);
}

String data_name(const DataReg* reg, const DataType type) { return data_decl(reg, type)->id.name; }

usize data_size(const DataReg* reg, const DataType type) { return data_decl(reg, type)->size; }

usize data_align(const DataReg* reg, const DataType type) { return data_decl(reg, type)->align; }

usize data_meta_size(const DataReg* reg, const DataMeta meta) {
  switch (meta.container) {
  case DataContainer_None:
    return data_decl(reg, meta.type)->size;
  case DataContainer_Pointer:
    return sizeof(void*);
  case DataContainer_Array:
    return sizeof(DataArray);
  }
  diag_crash();
}

DataType data_reg_struct(DataReg* reg, const String name, const usize size, const usize align) {
  const DataId id = data_id_create(reg->alloc, name);

  diag_assert_msg(bits_ispow2(align), "Alignment '{}' is not a power-of-two", fmt_int(align));
  diag_assert_msg(
      bits_aligned(size, align),
      "Size '{}' is not a multiple of alignment '{}'",
      fmt_size(size),
      fmt_int(align));
  diag_assert_msg(
      sentinel_check(data_type_by_id(reg, id)), "Duplicate type with name '{}'", fmt_text(name));

  const DataType type           = data_alloc_type(reg);
  *data_decl_mutable(reg, type) = (DataDecl){
      .kind       = DataKind_Struct,
      .size       = size,
      .align      = align,
      .id         = id,
      .val_struct = {.fields = dynarray_create_t(reg->alloc, DataDeclField, 8)},
  };
  return type;
}

void data_reg_field(
    DataReg*       reg,
    const DataType parent,
    const String   name,
    const usize    offset,
    const DataMeta meta) {

  const DataId id         = data_id_create(reg->alloc, name);
  DataDecl*    parentDecl = data_decl_mutable(reg, parent);

  diag_assert_msg(parentDecl->kind == DataKind_Struct, "Field parent has to be a Struct");
  diag_assert_msg(
      offset + data_meta_size(reg, meta) <= data_decl(reg, parent)->size,
      "Offset '{}' is out of bounds for the Struct type",
      fmt_int(offset));

  *dynarray_push_t(&parentDecl->val_struct.fields, DataDeclField) = (DataDeclField){
      .id     = id,
      .offset = offset,
      .meta   = meta,
  };
}

DataType data_reg_union(
    DataReg* reg, const String name, const usize size, const usize align, const usize tagOffset) {
  const DataId id = data_id_create(reg->alloc, name);

  diag_assert_msg(bits_ispow2(align), "Alignment '{}' is not a power-of-two", fmt_int(align));
  diag_assert_msg(
      bits_aligned(size, align),
      "Size '{}' is not a multiple of alignment '{}'",
      fmt_size(size),
      fmt_int(align));
  diag_assert_msg(
      sentinel_check(data_type_by_id(reg, id)), "Duplicate type with name '{}'", fmt_text(name));

  const DataType type           = data_alloc_type(reg);
  *data_decl_mutable(reg, type) = (DataDecl){
      .kind  = DataKind_Union,
      .size  = size,
      .align = align,
      .id    = id,
      .val_union =
          {
              .tagOffset = tagOffset,
              .choices   = dynarray_create_t(reg->alloc, DataDeclChoice, 8),
          },
  };
  return type;
}

void data_reg_choice(
    DataReg*       reg,
    const DataType parent,
    const String   name,
    const i32      tag,
    const usize    offset,
    const DataMeta meta) {

  const DataId id         = data_id_create(reg->alloc, name);
  DataDecl*    parentDecl = data_decl_mutable(reg, parent);

  diag_assert_msg(parentDecl->kind == DataKind_Union, "Choice parent has to be a Union");
  diag_assert_msg(
      offset + data_meta_size(reg, meta) <= data_decl(reg, parent)->size,
      "Offset '{}' is out of bounds for the Union type",
      fmt_int(offset));

  *dynarray_push_t(&parentDecl->val_union.choices, DataDeclChoice) = (DataDeclChoice){
      .id     = id,
      .tag    = tag,
      .offset = offset,
      .meta   = meta,
  };
}

DataType data_reg_enum(DataReg* reg, const String name) {
  const DataId id = data_id_create(reg->alloc, name);
  diag_assert_msg(
      sentinel_check(data_type_by_id(reg, id)), "Duplicate type with name '{}'", fmt_text(name));

  const DataType type           = data_alloc_type(reg);
  *data_decl_mutable(reg, type) = (DataDecl){
      .kind     = DataKind_Enum,
      .size     = sizeof(i32),
      .align    = alignof(i32),
      .id       = id,
      .val_enum = {.consts = dynarray_create_t(reg->alloc, DataDeclConst, 8)},
  };
  return type;
}

void data_reg_const(DataReg* reg, const DataType parent, const String name, const i32 value) {
  const DataId id         = data_id_create(reg->alloc, name);
  DataDecl*    parentDecl = data_decl_mutable(reg, parent);

  diag_assert_msg(parentDecl->kind == DataKind_Enum, "Constant parent has to be an Enum");

  *dynarray_push_t(&parentDecl->val_enum.consts, DataDeclConst) =
      (DataDeclConst){.id = id, .value = value};
}

DataMeta data_meta_base(const DataMeta meta) { return (DataMeta){.type = meta.type}; }

const DataDecl* data_decl(const DataReg* reg, const DataType type) {
  diag_assert_msg(type, "Uninitialized data-type");
  return dynarray_at_t(&reg->types, type - 1, DataDecl);
}

Mem data_field_mem(const DataReg* reg, const DataDeclField* field, Mem structMem) {
  return mem_create(
      bits_ptr_offset(structMem.ptr, field->offset), data_meta_size(reg, field->meta));
}

i32* data_union_tag(const DataDeclUnion* decl, const Mem unionMem) {
  return (i32*)bits_ptr_offset(unionMem.ptr, decl->tagOffset);
}

Mem data_choice_mem(const DataReg* reg, const DataDeclChoice* choice, const Mem unionMem) {
  return mem_create(
      bits_ptr_offset(unionMem.ptr, choice->offset), data_meta_size(reg, choice->meta));
}

Mem data_elem_mem(const DataDecl* decl, const DataArray* array, const usize index) {
  return mem_create(bits_ptr_offset(array->values, decl->size * index), decl->size);
}
