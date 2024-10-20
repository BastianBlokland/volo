#pragma once
#include "core_annotation.h"
#include "core_string.h"

/**
 * Largest supported alignment for mem data-type allocations.
 */
#define data_type_mem_align_max 16

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
  X(f16)                                                                                           \
  X(f32)                                                                                           \
  X(f64)                                                                                           \
  X(String)                                                                                        \
  X(StringHash)                                                                                    \
  X(DataMem)

typedef u32 DataType;

typedef struct {
  bool  external; // Allocation is not managed by the data library.
  void* ptr;
  usize size;
} DataMem;

// clang-format off

typedef enum {
  DataKind_Invalid,
#define X(_T_) DataKind_##_T_,
  DATA_PRIMS
#undef X

  DataKind_Struct,
  DataKind_Union,
  DataKind_Enum,
  DataKind_Opaque,

  DataKind_Count,
} DataKind;

// clang-format on

MAYBE_UNUSED INLINE_HINT static DataMem data_mem_create(const Mem mem) {
  return (DataMem){.ptr = mem.ptr, .size = mem.size};
}

MAYBE_UNUSED INLINE_HINT static DataMem data_mem_create_ext(const Mem mem) {
  return (DataMem){.external = true, .ptr = mem.ptr, .size = mem.size};
}

MAYBE_UNUSED INLINE_HINT static Mem data_mem(const DataMem dataMem) {
  return mem_create(dataMem.ptr, dataMem.size);
}
