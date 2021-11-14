#pragma once
#include "core_string.h"

typedef struct sDataType DataType;

#define data_type_extern(_NAME_) extern DataType* g_data_type_##_NAME_
#define data_type_define(_NAME_) DataType* g_data_type_##_NAME_
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

typedef struct {
  String name;
  usize  size, align;
} DataTypeConfig;

typedef struct sDataTypeStructBuilder DataTypeStructBuilder;
typedef struct sDataTypeEnumBuilder   DataTypeEnumBuilder;

typedef void (*DataTypeStructInit)(DataTypeStructBuilder*);
typedef void (*DataTypeEnumInit)(DataTypeEnumBuilder*);

#define data_register_struct_t(_T_, _INIT_, ...)                                                   \
  data_register(                                                                                   \
      (_INIT_),                                                                                    \
      data_type_ptr(_T_),                                                                          \
      &(DataTypeConfig){                                                                           \
          .name = string_lit(#_T_), .size = sizeof(_T_), .align = alignof(_T_), ##__VA_ARGS__})

void data_register_struct(DataTypeStructInit, DataType**, const DataTypeConfig*);

#define data_register_enum_t(_T_, _INIT_, ...)                                                     \
  data_register(                                                                                   \
      (_INIT_),                                                                                    \
      data_type_ptr(_T_),                                                                          \
      &(DataTypeConfig){                                                                           \
          .name = string_lit(#_T_), .size = sizeof(i32), .align = alignof(i32), ##__VA_ARGS__})

void data_register_enum(DataTypeEnumInit, DataType**, const DataTypeConfig*);
