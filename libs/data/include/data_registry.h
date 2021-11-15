#pragma once
#include "core_string.h"

#define DATA_PRIMS                                                                                 \
  X(bool)                                                                                          \
  X(i8)                                                                                            \
  X(i16)                                                                                           \
  X(i32)                                                                                           \
  X(i64)                                                                                           \
  X(u8)                                                                                            \
  X(u16)                                                                                           \
  X(u32)                                                                                           \
  X(u64)                                                                                           \
  X(f32)                                                                                           \
  X(f64)                                                                                           \
  X(String)

typedef u32 DataType;

typedef struct {
  String name;
  u32    hash;
} DataId;

typedef struct {
  void* data;
  usize count;
} DataArray;

typedef enum {
  DataContainer_None,
  DataContainer_Array,
} DataContainer;

typedef struct {
  DataType      type;
  DataContainer container;
} DataMeta;

// clang-format off

typedef enum {
  DataKind_Invalid,
#define X(_T_) DataKind_##_T_,
  DATA_PRIMS
#undef X

  DataKind_Struct,
  DataKind_Enum,

  DataKind_Count,
} DataKind;


typedef enum {
#define X(_T_) DataPrim_##_T_,
  DATA_PRIMS
#undef X

  DataPrim_Count,
} DataPrim;

// clang-format on

/**
 * TODO:
 */
DataType data_type_prim(DataPrim);

String data_kind_str(DataKind);

/**
 * TODO:
 * Returns sentinel_u32 if not found.
 */
DataType data_type_by_name(String name);
DataType data_type_by_hash(u32 nameHash);

DataKind data_type_kind(DataType);
DataId   data_type_id(DataType);
usize    data_type_size(DataType);
usize    data_type_align(DataType);
usize    data_type_fields(DataType);
usize    data_type_consts(DataType);

DataType data_register_struct(String name, usize size, usize align);
void     data_register_field(DataType parentId, String name, usize offset, DataMeta);
DataType data_register_enum(String name);
void     data_register_const(DataType parentId, String name, i32 value);
