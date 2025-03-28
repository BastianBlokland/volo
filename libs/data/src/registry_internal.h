#pragma once
#include "core_array.h"
#include "core_dynarray.h"
#include "data_registry.h"

typedef struct {
  String     name;
  StringHash hash;
} DataId;

typedef struct {
  DataId   id;
  usize    offset;
  DataMeta meta;
} DataDeclField;

typedef struct {
  bool     hasHole; // Fields do not cover all bytes of the struct.
  DynArray fields;  // DataDeclField[]
} DataDeclStruct;

typedef struct {
  DataId   id;
  i32      tag;
  usize    offset;
  DataMeta meta;
} DataDeclChoice;

typedef struct {
  usize             tagOffset;
  DataUnionNameType nameType;
  usize             nameOffset;
  DynArray          choices; // DataDeclChoice[]
} DataDeclUnion;

typedef struct {
  DataId id;
  i32    value;
} DataDeclConst;

typedef struct {
  bool     multi;  // Multiple constants can be active at the same time.
  DynArray consts; // DataDeclConst[]
} DataDeclEnum;

typedef struct {
  DataId         id;
  DataKind       kind;
  usize          size, align;
  String         comment;
  DataNormalizer normalizer;
  union {
    DataDeclStruct val_struct;
    DataDeclUnion  val_union;
    DataDeclEnum   val_enum;
  };
} DataDecl;

struct sDataReg {
  DynArray   types; // DataDecl[]
  Allocator* alloc;
};

void data_reg_global_init(void);
void data_reg_global_teardown(void);

/**
 * Strip off any container or other special attributes from the meta.
 */
DataMeta data_meta_base(DataMeta);

/**
 * Lookup a declaration for a type.
 */
const DataDecl* data_decl(const DataReg*, DataType);

MAYBE_UNUSED static const DataDecl* data_decl_unchecked(const DataReg* reg, const DataType type) {
  return dynarray_begin_t(&reg->types, DataDecl) + (type - 1);
}

/**
 * Create a memory view over a field in a structure.
 */
Mem data_field_mem(const DataReg*, const DataDeclField*, Mem structMem);

/**
 * Create a pointer to the tag value of the given union.
 */
i32* data_union_tag(const DataDeclUnion*, Mem unionMem);

/**
 * Create a pointer to the union name.
 * NOTE: Returns null if the union has no name or the name is not of the right type.
 */
String*           data_union_name_string(const DataDeclUnion*, Mem unionMem);
StringHash*       data_union_name_hash(const DataDeclUnion*, Mem unionMem);
DataUnionNameType data_union_name_type(const DataDeclUnion*);

/**
 * Find a choice with the given tag.
 */
const DataDeclChoice* data_choice_from_tag(const DataDeclUnion*, i32 tag);

/**
 * Create a memory view over a choice in a union.
 */
Mem data_choice_mem(const DataReg*, const DataDeclChoice*, Mem unionMem);

/**
 * Create a memory view over an element in the given array.
 */
Mem data_elem_mem(const DataDecl*, const HeapArray*, usize index);

/**
 * Find a constant in the enum with the given id/value.
 */
const DataDeclConst* data_const_from_id(const DataDeclEnum*, StringHash id);
const DataDeclConst* data_const_from_val(const DataDeclEnum*, i32 val);

/**
 * Check if the given struct can be inlined into its parent.
 * NOTE: When struct can be inlined the field to inline is returned, otherwise null is returned.
 */
const DataDeclField* data_struct_inline_field(const DataDeclStruct*);
