#include "core_compare.h"
#include "core_math.h"

#define COMPARE_DEFINE_ARITHMETIC_WITH_NAME(_TYPE_, _NAME_)                                        \
  i8 compare_##_NAME_(const void* a, const void* b) {                                              \
    const _TYPE_ aVal = *(const _TYPE_*)a;                                                         \
    const _TYPE_ bVal = *(const _TYPE_*)b;                                                         \
    return aVal < bVal ? -1 : aVal > bVal ? 1 : 0;                                                 \
  }                                                                                                \
  i8 compare_##_NAME_##_reverse(const void* a, const void* b) { return compare_##_NAME_(b, a); }

#define COMPARE_DEFINE_ARITHMETIC(_TYPE_) COMPARE_DEFINE_ARITHMETIC_WITH_NAME(_TYPE_, _TYPE_)

COMPARE_DEFINE_ARITHMETIC(i8)
COMPARE_DEFINE_ARITHMETIC(i16)
COMPARE_DEFINE_ARITHMETIC(i32)
COMPARE_DEFINE_ARITHMETIC(i64)
COMPARE_DEFINE_ARITHMETIC(u8)
COMPARE_DEFINE_ARITHMETIC(u16)
COMPARE_DEFINE_ARITHMETIC(u32)
COMPARE_DEFINE_ARITHMETIC(u64)
COMPARE_DEFINE_ARITHMETIC(usize)
COMPARE_DEFINE_ARITHMETIC(f32)
COMPARE_DEFINE_ARITHMETIC(f64)
COMPARE_DEFINE_ARITHMETIC_WITH_NAME(StringHash, stringhash)

i8 compare_string(const void* a, const void* b) {
  return string_cmp(*(const String*)a, *(const String*)b);
}

i8 compare_string_reverse(const void* a, const void* b) { return compare_string(b, a); }
