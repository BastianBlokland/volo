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

f32 math_mod_f32(const f32 x, const f32 y) { return intrinsic_fmod_f32(x, y); }

f32 math_sqrt_f32(const f32 val) { return intrinsic_sqrt_f32(val); }
f64 math_sqrt_f64(const f64 val) { return intrinsic_sqrt_f64(val); }

f32 math_log_f32(const f32 val) { return intrinsic_log_f32(val); }

f32 math_sin_f32(const f32 val) { return intrinsic_sin_f32(val); }

f32 math_asin_f32(const f32 val) { return intrinsic_asin_f32(val); }

f32 math_cos_f32(const f32 val) { return intrinsic_cos_f32(val); }

f32 math_acos_f32(const f32 val) { return intrinsic_acos_f32(val); }

f32 math_tan_f32(const f32 val) { return intrinsic_tan_f32(val); }

f32 math_atan_f32(const f32 val) { return intrinsic_atan_f32(val); }

f32 math_atan2_f32(const f32 x, const f32 y) { return intrinsic_atan2_f32(x, y); }

f32 math_pow_f32(const f32 base, const f32 exp) { return intrinsic_pow_f32(base, exp); }

f32 math_exp_f32(const f32 exp) { return intrinsic_exp_f32(exp); }

f32 math_trunc_f32(const f32 val) { return (i32)val; }
f64 math_trunc_f64(const f64 val) { return (i64)val; }

f32 math_round_nearest_f32(const f32 val) { return intrinsic_round_nearest_f32(val); }
f64 math_round_nearest_f64(const f64 val) { return intrinsic_round_nearest_f64(val); }

f32 math_round_down_f32(const f32 val) { return intrinsic_round_down_f32(val); }
f64 math_round_down_f64(const f64 val) { return intrinsic_round_down_f64(val); }

f32 math_round_up_f32(const f32 val) { return intrinsic_round_up_f32(val); }
f64 math_round_up_f64(const f64 val) { return intrinsic_round_up_f64(val); }

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

bool math_towards_f32(f32* val, const f32 target, const f32 maxDelta) {
  if (math_abs(target - *val) <= maxDelta) {
    *val = target;
    return true;
  }
  *val += math_sign(target - *val) * maxDelta;
  return false;
}
