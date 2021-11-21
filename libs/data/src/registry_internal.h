#pragma once
#include "data_registry.h"

typedef struct {
  String name;
  u32    hash;
} DataId;

typedef struct {
  DataId   id;
  usize    offset;
  DataMeta meta;
} DataDeclField;

typedef struct {
  DataDeclField* fields;
  usize          count;
} DataDeclStruct;

typedef struct {
  DataId id;
  i32    value;
} DataDeclConst;

typedef struct {
  DataDeclConst* consts;
  usize          count;
} DataDeclEnum;

typedef struct {
  DataKind kind;
  usize    size, align;
  DataId   id;
  union {
    DataDeclStruct val_struct;
    DataDeclEnum   val_enum;
  };
} DataDecl;

/**
 * Iterate over all fields in the structure.
 * NOTE: _TYPE_ is expanded twice, so care must be taken when providing complex expressions.
 */
#define data_for_fields(_TYPE_, _VAR_, ...)                                                        \
  {                                                                                                \
    diag_assert(data_decl(_TYPE_)->kind == DataKind_Struct);                                       \
    const DataDeclField* _VAR_       = data_decl(_TYPE_)->val_struct.fields;                       \
    const DataDeclField* _VAR_##_end = _VAR_ + data_decl(_TYPE_)->val_struct.count;                \
    for (; _VAR_ != _VAR_##_end; ++_VAR_) {                                                        \
      __VA_ARGS__                                                                                  \
    }                                                                                              \
  }

/**
 * Strip of any container or other special attributes from the meta.
 */
DataMeta data_meta_base(DataMeta);

/**
 * Lookup a declaration for a type.
 */
const DataDecl* data_decl(DataType);

/**
 * Create a memory view over a field in a structure.
 */
Mem data_field_mem(const DataDeclField*, Mem structMem);

/**
 * Create a memory view over an element in the given array.
 */
Mem data_elem_mem(const DataDecl*, const DataArray*, usize index);
