#include "core_annotation.h"
#include "core_types.h"

ASSERT(sizeof(i8) == 1, "Unexpected sizeof i8 typedef")
ASSERT(sizeof(u8) == 1, "Unexpected sizeof u8 typedef")
ASSERT(sizeof(bool) == 1, "Unexpected sizeof bool typedef")

ASSERT(sizeof(i16) == 2, "Unexpected sizeof i16 typedef")
ASSERT(sizeof(u16) == 2, "Unexpected sizeof u16 typedef")
ASSERT(sizeof(f16) == 2, "Unexpected sizeof f16 typedef")

ASSERT(sizeof(i32) == 4, "Unexpected sizeof i32 typedef")
ASSERT(sizeof(u32) == 4, "Unexpected sizeof u32 typedef")
ASSERT(sizeof(f32) == 4, "Unexpected sizeof f32 typedef")

ASSERT(sizeof(i64) == 8, "Unexpected sizeof i64 typedef")
ASSERT(sizeof(u64) == 8, "Unexpected sizeof u64 typedef")
ASSERT(sizeof(f64) == 8, "Unexpected sizeof f64 typedef")

#ifdef VOLO_32_BIT
ASSERT(sizeof(usize) == 4, "Unexpected sizeof usize typedef")
ASSERT(sizeof(iptr) == 4, "Unexpected sizeof iptr typedef")
ASSERT(sizeof(uptr) == 4, "Unexpected sizeof uptr typedef")
#else
ASSERT(sizeof(usize) == 8, "Unexpected sizeof usize typedef")
ASSERT(sizeof(iptr) == 8, "Unexpected sizeof iptr typedef")
ASSERT(sizeof(uptr) == 8, "Unexpected sizeof uptr typedef")
#endif

ASSERT(sizeof(null) == sizeof(uptr), "Unexpected size of null")
