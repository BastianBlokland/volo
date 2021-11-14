#pragma once
#include "core_string.h"

typedef enum {
  DataKind_Primitive,
  DataKind_Struct,
  DataKind_Enum,
} DataKind;

typedef u32 DataType;

#define data_type_extern(_NAME_) extern DataType g_data_type_##_NAME_
#define data_type_define(_NAME_) DataType g_data_type_##_NAME_
#define data_type_ptr(_NAME_) &g_data_type_##_NAME_

#define DATA_PRIMS                                                                                 \
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

#define X(_T_) data_type_extern(_T_);
DATA_PRIMS
#undef X

DataKind data_type_kind(DataType);
String   data_type_name(DataType);
usize    data_type_size(DataType);
usize    data_type_align(DataType);

typedef struct {
  String name;
  usize  size, align;
} DataTypeConfig;

typedef struct {
  String    name;
  usize     offset;
  DataType* type;
} DataStructFieldConfig;

DataType data_type_register_struct(
    DataType*, const DataTypeConfig*, const DataStructFieldConfig*, usize fieldCount);

typedef struct {
  String name;
  i32    value;
} DataEnumEntryConfig;

DataType data_type_register_enum(
    DataType*, const DataTypeConfig*, const DataEnumEntryConfig*, usize entryCount);
