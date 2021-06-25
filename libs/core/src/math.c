#include "core_array.h"
#include "core_diag.h"
#include "core_math.h"

#include <math.h>

u64 math_pow10_u64(const u8 val) {
  static u64 table[] = {
      1ull,
      10ull,
      100ull,
      1000ull,
      10000ull,
      100000ull,
      1000000ull,
      10000000ull,
      100000000ull,
      1000000000ull,
      10000000000ull,
      100000000000ull,
      1000000000000ull,
      10000000000000ull,
      100000000000000ull,
      1000000000000000ull,
      10000000000000000ull,
      100000000000000000ull,
      1000000000000000000ull,
      10000000000000000000ull,
  };
  diag_assert(val < array_elems(table));
  return table[val];
}

f32 math_sqrt_f32(const f32 val) { return sqrtf(val); }

f32 math_log_f32(const f32 val) { return logf(val); }

f32 math_sin_f32(const f32 val) { return sinf(val); }

f32 math_cos_f32(const f32 val) { return cosf(val); }
