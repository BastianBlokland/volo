#include "core_array.h"
#include "core_diag.h"
#include "core_math.h"

#include <math.h>

u64 math_pow10_u64(const u8 val) {
  static u64 table[] = {
      u64_c(1),
      u64_c(10),
      u64_c(100),
      u64_c(1000),
      u64_c(10000),
      u64_c(100000),
      u64_c(1000000),
      u64_c(10000000),
      u64_c(100000000),
      u64_c(1000000000),
      u64_c(10000000000),
      u64_c(100000000000),
      u64_c(1000000000000),
      u64_c(10000000000000),
      u64_c(100000000000000),
      u64_c(1000000000000000),
      u64_c(10000000000000000),
      u64_c(100000000000000000),
      u64_c(1000000000000000000),
      u64_c(10000000000000000000),
  };
  diag_assert(val < array_elems(table));
  return table[val];
}

f32 math_sqrt_f32(const f32 val) { return sqrtf(val); }

f32 math_log_f32(const f32 val) { return logf(val); }

f32 math_sin_f32(const f32 val) { return sinf(val); }

f32 math_cos_f32(const f32 val) { return cosf(val); }
