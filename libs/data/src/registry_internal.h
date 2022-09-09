#pragma once
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
  DynArray fields; // DataDeclField[]
} DataDeclStruct;

typedef struct {
  DataId   id;
  i32      tag;
  usize    offset;
  DataMeta meta;
} DataDeclChoice;

typedef struct {
  usize    tagOffset;
  DynArray choices; // DataDeclChoice[]
} DataDeclUnion;

typedef struct {
  DataId id;
  i32    value;
} DataDeclConst;

typedef struct {
  DynArray consts; // DataDeclConst[]
} DataDeclEnum;

typedef struct {
  DataKind kind;
  usize    size, align;
  DataId   id;
  union {
    DataDeclStruct val_struct;
    DataDeclUnion  val_union;
    DataDeclEnum   val_enum;
  };
} DataDecl;

/**
 * Strip off any container or other special attributes from the meta.
 */
DataMeta data_meta_base(DataMeta);

/**
 * Lookup a declaration for a type.
 */
const DataDecl* data_decl(const DataReg*, DataType);

/**
 * Create a memory view over a field in a structure.
 */
Mem data_field_mem(const DataReg*, const DataDeclField*, Mem structMem);

/**
 * Create a pointer to the tag value of the given union.
 */
i32* data_union_tag(const DataDeclUnion*, Mem unionMem);

/**
 * Create a memory view over a choice in a union.
 */
Mem data_choice_mem(const DataReg*, const DataDeclChoice*, Mem unionMem);

/**
 * Create a memory view over an element in the given array.
 */
Mem data_elem_mem(const DataDecl*, const DataArray*, usize index);
