#include "core_alloc.h"
#include "core_bits.h"
#include "core_bitset.h"
#include "core_diag.h"
#include "data_registry.h"

#include "registry_internal.h"

static DataId data_id_create(Allocator* alloc, const String name) {
  return (DataId){.name = string_dup(alloc, name), .hash = string_hash(name)};
}

static void data_id_destroy(Allocator* alloc, const DataId id) { string_free(alloc, id.name); }

static DataDecl* data_decl_mutable(DataReg* reg, const DataType type) {
  diag_assert_msg(type, "Uninitialized data-type");
  return dynarray_at_t(&reg->types, type - 1, DataDecl);
}

static DataType data_type_alloc(DataReg* reg, const String name) {
  *dynarray_push_t(&reg->types, DataDecl) = (DataDecl){
      .id = data_id_create(reg->alloc, name),
  };
  return (DataType)reg->types.size;
}

static DataType data_type_declare(DataReg* reg, const String name) {
  const StringHash nameHash = string_hash(name);
  for (DataType type = 1; type != (reg->types.size + 1); ++type) {
    if (data_decl_mutable(reg, type)->id.hash == nameHash) {
      return type;
    }
  }
  return data_type_alloc(reg, name);
}

static bool data_struct_has_hole(const DataReg* reg, DataDecl* decl) {
  diag_assert(decl->kind == DataKind_Struct);
  BitSet filledSet = mem_stack(bits_to_bytes(decl->size) + 1);
  bitset_clear_all(filledSet);

  dynarray_for_t(&decl->val_struct.fields, DataDeclField, fieldDecl) {
    const usize fieldSize = data_meta_size(reg, fieldDecl->meta);
    for (usize i = 0; i != fieldSize; ++i) {
      const usize index = fieldDecl->offset + i;
      diag_assert_msg(!bitset_test(filledSet, index), "Struct has overlapping fields");
      bitset_set(filledSet, index);
    }
  }

  return bitset_count(filledSet) != decl->size;
}

/**
 * To support field-names that are stored in a nested struct we only preserve the last name.
 * For example: 'container.myField' turns into 'myField'.
 */
static String data_reg_trim_field_name(const String fieldName) {
  static const String g_separators  = string_static(".");
  const usize         lastSeparator = string_find_last_any(fieldName, g_separators);
  if (sentinel_check(lastSeparator)) {
    return fieldName; // Does not contain any separators.
  }
  return string_consume(fieldName, lastSeparator + 1 /* separatorLength */);
}

DataReg* g_dataReg;

void data_reg_global_init(void) { g_dataReg = data_reg_create(g_allocHeap); }

void data_reg_global_teardown(void) {
  data_reg_destroy(g_dataReg);
  g_dataReg = null;
}

DataReg* data_reg_create(Allocator* alloc) {
  DataReg* reg = alloc_alloc_t(alloc, DataReg);
  *reg         = (DataReg){
              .types = dynarray_create_t(alloc, DataDecl, 64),
              .alloc = alloc,
  };

#define X(_T_)                                                                                     \
  const DataType type_##_T_                 = data_type_alloc(reg, string_lit(#_T_));              \
  data_decl_mutable(reg, type_##_T_)->kind  = DataKind_##_T_;                                      \
  data_decl_mutable(reg, type_##_T_)->size  = sizeof(_T_);                                         \
  data_decl_mutable(reg, type_##_T_)->align = alignof(_T_);

  DATA_PRIMS
#undef X

  return reg;
}

void data_reg_destroy(DataReg* reg) {

  dynarray_for_t(&reg->types, DataDecl, decl) {
    data_id_destroy(reg->alloc, decl->id);
    string_maybe_free(reg->alloc, decl->comment);
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

u32 data_type_count(const DataReg* reg) { return (u32)reg->types.size; }

DataType data_type_from_name(const DataReg* reg, const String name) {
  return data_type_from_name_hash(reg, string_hash(name));
}

DataType data_type_from_name_hash(const DataReg* reg, const StringHash nameHash) {
  for (DataType type = 1; type != (reg->types.size + 1); ++type) {
    if (data_decl(reg, type)->id.hash == nameHash) {
      return type;
    }
  }
  return 0;
}

String data_name(const DataReg* reg, const DataType type) { return data_decl(reg, type)->id.name; }

StringHash data_name_hash(const DataReg* reg, const DataType type) {
  return data_decl(reg, type)->id.hash;
}

String data_const_name(const DataReg* reg, const DataType enumType, const i32 value) {
  const DataDecl* decl = data_decl(reg, enumType);
  diag_assert(decl->kind == DataKind_Enum);
  dynarray_for_t(&decl->val_enum.consts, DataDeclConst, constDecl) {
    if (constDecl->value == value) {
      return constDecl->id.name;
    }
  }
  return string_empty;
}

usize data_size(const DataReg* reg, const DataType type) { return data_decl(reg, type)->size; }

usize data_align(const DataReg* reg, const DataType type) { return data_decl(reg, type)->align; }

String data_comment(const DataReg* reg, const DataType type) {
  return data_decl(reg, type)->comment;
}

usize data_meta_size(const DataReg* reg, const DataMeta meta) {
  switch (meta.container) {
  case DataContainer_None:
    return data_decl(reg, meta.type)->size;
  case DataContainer_Pointer:
    return sizeof(void*);
  case DataContainer_InlineArray:
    return data_decl(reg, meta.type)->size * meta.fixedCount;
  case DataContainer_HeapArray:
    return sizeof(HeapArray);
  case DataContainer_DynArray:
    return sizeof(DynArray);
  }
  diag_crash();
}

usize data_meta_align(const DataReg* reg, const DataMeta meta) {
  switch (meta.container) {
  case DataContainer_None:
    return data_decl(reg, meta.type)->align;
  case DataContainer_Pointer:
    return alignof(void*);
  case DataContainer_InlineArray:
    return data_decl(reg, meta.type)->align;
  case DataContainer_HeapArray:
    return alignof(HeapArray);
  case DataContainer_DynArray:
    return alignof(DynArray);
  }
  diag_crash();
}

DataType data_declare(DataReg* reg, const String name) {
  diag_assert_msg(name.size, "Type name cannot be empty");
  return data_type_declare(reg, name);
}

DataType data_reg_struct(DataReg* reg, const String name, const usize size, const usize align) {
  diag_assert_msg(name.size, "Type name cannot be empty");
  diag_assert_msg(bits_ispow2(align), "Alignment '{}' is not a power-of-two", fmt_int(align));
  diag_assert_msg(
      bits_aligned(size, align),
      "Size '{}' is not a multiple of alignment '{}'",
      fmt_size(size),
      fmt_int(align));

  const DataType type = data_type_declare(reg, name);
  DataDecl*      decl = data_decl_mutable(reg, type);
  diag_assert_msg(!decl->kind, "Type '{}' already defined", fmt_text(decl->id.name));
  decl->kind       = DataKind_Struct;
  decl->size       = size;
  decl->align      = align;
  decl->val_struct = (DataDeclStruct){
      .hasHole = true,
      .fields  = dynarray_create_t(reg->alloc, DataDeclField, 8),
  };
  return type;
}

void data_reg_field(
    DataReg*       reg,
    const DataType parent,
    String         name,
    const usize    size,
    const usize    offset,
    const DataMeta meta) {
  DataDecl* parentDecl = data_decl_mutable(reg, parent);

  (void)size;

  /**
   * Support field registrations that are contained in a container struct.
   * For example: You can register 'container.myField'; in this case we ignore the container struct
   * completely (and the name will become 'myField').
   */
  name = data_reg_trim_field_name(name);

  diag_assert_msg(name.size, "Field name cannot be empty");
  diag_assert_msg(parentDecl->kind == DataKind_Struct, "Field parent has to be a Struct");
  diag_assert_msg(size == data_meta_size(reg, meta), "Mismatched field size");
  diag_assert_msg(
      offset + size <= data_decl(reg, parent)->size,
      "Offset '{}' is out of bounds for the Struct type",
      fmt_int(offset));

  *dynarray_push_t(&parentDecl->val_struct.fields, DataDeclField) = (DataDeclField){
      .id     = data_id_create(reg->alloc, name),
      .offset = offset,
      .meta   = meta,
  };

  /**
   * Test if this field has filled the last hole in the struct.
   * For structs without holes we can skip the mem-set during binary data reading.
   */
  parentDecl->val_struct.hasHole = data_struct_has_hole(reg, parentDecl);
}

DataType data_reg_union(
    DataReg* reg, const String name, const usize size, const usize align, const usize tagOffset) {

  diag_assert_msg(name.size, "Type name cannot be empty");
  diag_assert_msg(bits_ispow2(align), "Alignment '{}' is not a power-of-two", fmt_int(align));
  diag_assert_msg(
      bits_aligned(size, align),
      "Size '{}' is not a multiple of alignment '{}'",
      fmt_size(size),
      fmt_int(align));

  const DataType type = data_type_declare(reg, name);
  DataDecl*      decl = data_decl_mutable(reg, type);
  diag_assert_msg(!decl->kind, "Type '{}' already defined", fmt_text(decl->id.name));
  decl->kind      = DataKind_Union;
  decl->size      = size;
  decl->align     = align;
  decl->val_union = (DataDeclUnion){
      .tagOffset  = tagOffset,
      .nameOffset = sentinel_usize,
      .choices    = dynarray_create_t(reg->alloc, DataDeclChoice, 8),
  };
  return type;
}

void data_reg_union_name(
    DataReg* reg, const DataType parent, const usize nameOffset, const DataUnionNameType nameType) {
  diag_assert(!sentinel_check(nameOffset));
  diag_assert(nameType != DataUnionNameType_None);

  DataDecl* parentDecl = data_decl_mutable(reg, parent);
  diag_assert_msg(parentDecl->kind == DataKind_Union, "Union name parent has to be a Union");

  parentDecl->val_union.nameType   = nameType;
  parentDecl->val_union.nameOffset = nameOffset;
}

void data_reg_choice(
    DataReg*       reg,
    const DataType parent,
    const String   name,
    const i32      tag,
    const usize    size,
    const usize    offset,
    const DataMeta meta) {
  DataDecl* parentDecl = data_decl_mutable(reg, parent);

  (void)size;

  diag_assert_msg(name.size, "Choice name cannot be empty");
  diag_assert_msg(parentDecl->kind == DataKind_Union, "Choice parent has to be a Union");
  diag_assert_msg(!data_choice_from_tag(&parentDecl->val_union, tag), "Duplicate choice");

  MAYBE_UNUSED const bool emptyChoice = meta.type == 0;
  diag_assert_msg(emptyChoice || size == data_meta_size(reg, meta), "Mismatched field size");
  diag_assert_msg(
      emptyChoice || (offset + size <= data_decl(reg, parent)->size),
      "Offset '{}' is out of bounds for the Union type",
      fmt_int(offset));

  *dynarray_push_t(&parentDecl->val_union.choices, DataDeclChoice) = (DataDeclChoice){
      .id     = data_id_create(reg->alloc, name),
      .tag    = tag,
      .offset = offset,
      .meta   = meta,
  };
}

DataType data_reg_enum(DataReg* reg, const String name, const bool multi) {
  diag_assert_msg(name.size, "Type name cannot be empty");

  const DataType type = data_type_declare(reg, name);
  DataDecl*      decl = data_decl_mutable(reg, type);
  diag_assert_msg(!decl->kind, "Type '{}' already defined", fmt_text(decl->id.name));
  decl->kind     = DataKind_Enum;
  decl->size     = sizeof(i32);
  decl->align    = alignof(i32);
  decl->val_enum = (DataDeclEnum){
      .multi  = multi,
      .consts = dynarray_create_t(reg->alloc, DataDeclConst, 8),
  };
  return type;
}

void data_reg_const(DataReg* reg, const DataType parent, const String name, const i32 value) {
  diag_assert_msg(name.size, "Constant name cannot be empty");

  DataDecl* parentDecl = data_decl_mutable(reg, parent);
  diag_assert_msg(parentDecl->kind == DataKind_Enum, "Constant parent has to be an Enum");

  *dynarray_push_t(&parentDecl->val_enum.consts, DataDeclConst) = (DataDeclConst){
      .id    = data_id_create(reg->alloc, name),
      .value = value,
  };
}

DataType data_reg_opaque(DataReg* reg, const String name, const usize size, const usize align) {
  diag_assert_msg(name.size, "Type name cannot be empty");
  diag_assert_msg(bits_ispow2(align), "Alignment '{}' is not a power-of-two", fmt_int(align));
  diag_assert_msg(
      bits_aligned(size, align),
      "Size '{}' is not a multiple of alignment '{}'",
      fmt_size(size),
      fmt_int(align));

  const DataType type = data_type_declare(reg, name);
  DataDecl*      decl = data_decl_mutable(reg, type);
  diag_assert_msg(!decl->kind, "Type '{}' already defined", fmt_text(decl->id.name));
  decl->kind  = DataKind_Opaque;
  decl->size  = size;
  decl->align = align;
  return type;
}

void data_reg_comment(DataReg* reg, const DataType type, const String comment) {
  DataDecl* decl = data_decl_mutable(reg, type);
  diag_assert_msg(
      !decl->comment.size, "Comment already registered for type '{}'", fmt_text(decl->id.name));
  string_maybe_free(reg->alloc, decl->comment); // Don't leak when compiling without asserts.
  decl->comment = string_maybe_dup(reg->alloc, comment);
}

void data_reg_normalizer(DataReg* reg, const DataType type, const DataNormalizer normalizer) {
  DataDecl* decl = data_decl_mutable(reg, type);
  diag_assert_msg(
      !decl->normalizer, "Normalizer already registered for type '{}'", fmt_text(decl->id.name));
  decl->normalizer = normalizer;
}

DataMeta data_meta_base(const DataMeta meta) {
  return (DataMeta){.type = meta.type, .flags = meta.flags & DataFlags_TransferToBase};
}

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

String* data_union_name_string(const DataDeclUnion* decl, const Mem unionMem) {
  if (decl->nameType == DataUnionNameType_String) {
    diag_assert(!sentinel_check(decl->nameOffset));
    return (String*)bits_ptr_offset(unionMem.ptr, decl->nameOffset);
  }
  return null;
}

StringHash* data_union_name_hash(const DataDeclUnion* decl, const Mem unionMem) {
  if (decl->nameType == DataUnionNameType_StringHash) {
    diag_assert(!sentinel_check(decl->nameOffset));
    return (StringHash*)bits_ptr_offset(unionMem.ptr, decl->nameOffset);
  }
  return null;
}

DataUnionNameType data_union_name_type(const DataDeclUnion* decl) { return decl->nameType; }

const DataDeclChoice* data_choice_from_tag(const DataDeclUnion* unionDecl, const i32 tag) {
  dynarray_for_t(&unionDecl->choices, DataDeclChoice, choice) {
    if (choice->tag == tag) {
      return choice;
    }
  }
  return null;
}

Mem data_choice_mem(const DataReg* reg, const DataDeclChoice* choice, const Mem unionMem) {
  return mem_create(
      bits_ptr_offset(unionMem.ptr, choice->offset), data_meta_size(reg, choice->meta));
}

Mem data_elem_mem(const DataDecl* decl, const HeapArray* array, const usize index) {
  return mem_create(bits_ptr_offset(array->values, decl->size * index), decl->size);
}

const DataDeclConst* data_const_from_id(const DataDeclEnum* decl, const StringHash id) {
  dynarray_for_t(&decl->consts, DataDeclConst, constDecl) {
    if (constDecl->id.hash == id) {
      return constDecl;
    }
  }
  return null;
}

const DataDeclConst* data_const_from_val(const DataDeclEnum* decl, const i32 val) {
  dynarray_for_t(&decl->consts, DataDeclConst, constDecl) {
    if (constDecl->value == val) {
      return constDecl;
    }
  }
  return null;
}

const DataDeclField* data_struct_inline_field(const DataDeclStruct* decl) {
  if (decl->fields.size != 1) {
    return null; // Only structs with one field can be inlined.
  }
  const DataDeclField* field = dynarray_begin_t(&decl->fields, DataDeclField);
  if (field->meta.flags & DataFlags_InlineField) {
    return field;
  }
  return null; // Field was not marked as inlinable.
}
