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
  void* values;
  usize count;
} DataArray;

// clang-format off

typedef enum {
  DataKind_Invalid,
#define X(_T_) DataKind_##_T_,
  DATA_PRIMS
#undef X

  DataKind_Struct,
  DataKind_Union,
  DataKind_Enum,

  DataKind_Count,
} DataKind;

// clang-format on
