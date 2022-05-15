#include "core_array.h"
#include "core_diag.h"
#include "core_math.h"

#include "intrinsic_internal.h"

u64 math_pow10_u64(const u8 val) {
  static const u64 g_table[] = {
      u64_lit(1),
      u64_lit(10),
      u64_lit(100),
      u64_lit(1000),
      u64_lit(10000),
      u64_lit(100000),
      u64_lit(1000000),
      u64_lit(10000000),
      u64_lit(100000000),
      u64_lit(1000000000),
      u64_lit(10000000000),
      u64_lit(100000000000),
      u64_lit(1000000000000),
      u64_lit(10000000000000),
      u64_lit(100000000000000),
      u64_lit(1000000000000000),
      u64_lit(10000000000000000),
      u64_lit(100000000000000000),
      u64_lit(1000000000000000000),
      u64_lit(10000000000000000000),
  };
  diag_assert(val < array_elems(g_table));
  return g_table[val];
}

f32 math_mod_f32(const f32 x, const f32 y) { return intrinsic_fmodf(x, y); }

f32 math_sqrt_f32(const f32 val) { return intrinsic_sqrtf(val); }
f64 math_sqrt_f64(const f64 val) { return intrinsic_sqrt(val); }

f32 math_log_f32(const f32 val) { return intrinsic_logf(val); }

f32 math_sin_f32(const f32 val) { return intrinsic_sinf(val); }

f32 math_asin_f32(const f32 val) { return intrinsic_asinf(val); }

f32 math_cos_f32(const f32 val) { return intrinsic_cosf(val); }

f32 math_acos_f32(const f32 val) { return intrinsic_acosf(val); }

f32 math_tan_f32(const f32 val) { return intrinsic_tanf(val); }

f32 math_atan_f32(const f32 val) { return intrinsic_atanf(val); }

f32 math_atan2_f32(const f32 x, const f32 y) { return intrinsic_atan2f(x, y); }

f32 math_pow_f32(const f32 base, const f32 exp) { return intrinsic_powf(base, exp); }

f32 math_trunc_f32(const f32 val) { return (i32)val; }
f64 math_trunc_f64(const f64 val) { return (i64)val; }

f32 math_floor_f32(const f32 val) { return intrinsic_floorf(val); }
f64 math_floor_f64(const f64 val) { return intrinsic_floor(val); }

f32 math_ceil_f32(const f32 val) { return intrinsic_ceilf(val); }
f64 math_ceil_f64(const f64 val) { return intrinsic_ceil(val); }

f32 math_round_f32(const f32 val) { return intrinsic_roundf(val); }
f64 math_round_f64(const f64 val) { return intrinsic_round(val); }

f32 math_clamp_f32(const f32 val, const f32 min, const f32 max) {
  if (val <= min) {
    return min;
  }
  if (val >= max) {
    return max;
  }
  return val;
}

f64 math_clamp_f64(const f64 val, const f64 min, const f64 max) {
  if (val <= min) {
    return min;
  }
  if (val >= max) {
    return max;
  }
  return val;
}
