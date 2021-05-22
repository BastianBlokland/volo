#pragma once
#include "core_annotation.h"
#include "core_types.h"

/**
 * Sentinel values can be used to mark a value as being special. For example a function returning a
 * u32 can return u32_sentinel to indiate it failed.
 */

#define sentinel_i8 i8_max
#define sentinel_i16 i16_max
#define sentinel_i32 i32_max
#define sentinel_i64 i64_max
#define sentinel_u8 u8_max
#define sentinel_u16 u16_max
#define sentinel_u32 u32_max
#define sentinel_u64 u64_max
#define sentinel_usize usize_max

// clang-format off

/**
 * Check if the given value is equal to its sentinel value.
 * Pre-condition: '_VAL_' is a primitive integer type.
 */
#define sentinel_check(_VAL_) _Generic((_VAL_),                                                    \
    i8:   (_VAL_) == sentinel_i8,                                                                  \
    i16:  (_VAL_) == sentinel_i16,                                                                 \
    i32:  (_VAL_) == sentinel_i32,                                                                 \
    i64:  (_VAL_) == sentinel_i8,                                                                  \
    u8:   (_VAL_) == sentinel_u8,                                                                  \
    u16:  (_VAL_) == sentinel_u16,                                                                 \
    u32:  (_VAL_) == sentinel_u32,                                                                 \
    u64:  (_VAL_) == sentinel_u64                                                                  \
  )

// clang-format on
