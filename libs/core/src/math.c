#include "core/array.h"
#include "core/diag.h"
#include "core/intrinsic.h"
#include "core/math.h"

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
f64 math_mod_f64(const f64 x, const f64 y) { return intrinsic_fmod_f64(x, y); }

f32 math_sqrt_f32(const f32 val) { return intrinsic_sqrt_f32(val); }
f64 math_sqrt_f64(const f64 val) { return intrinsic_sqrt_f64(val); }

f32 math_cbrt_f32(const f32 val) { return intrinsic_cbrt_f32(val); }

f32 math_log_f32(const f32 val) { return intrinsic_log_f32(val); }

f32 math_log10_f32(const f32 val) { return intrinsic_log10_f32(val); }

f32 math_sin_f32(const f32 val) { return intrinsic_sin_f32(val); }
f64 math_sin_f64(const f64 val) { return intrinsic_sin_f64(val); }

f32 math_asin_f32(const f32 val) { return intrinsic_asin_f32(val); }

f32 math_cos_f32(const f32 val) { return intrinsic_cos_f32(val); }
f64 math_cos_f64(const f64 val) { return intrinsic_cos_f64(val); }

f32 math_acos_f32(const f32 val) { return intrinsic_acos_f32(val); }

f32 math_tan_f32(const f32 val) { return intrinsic_tan_f32(val); }

f32 math_atan_f32(const f32 val) { return intrinsic_atan_f32(val); }

f32 math_atan2_f32(const f32 x, const f32 y) { return intrinsic_atan2_f32(x, y); }

f32 math_pow_f32(const f32 base, const f32 exp) { return intrinsic_pow_f32(base, exp); }
f64 math_pow_f64(const f64 base, const f64 exp) { return intrinsic_pow_f64(base, exp); }

f32 math_pow_whole_f32(f32 base, u32 exp) {
  /**
   * Exponentiation by squaring.
   * Based on ivaigult's aswner:
   * https://stackoverflow.com/questions/48280854/how-to-force-powfloat-int-to-return-float
   */
  f32 result = 1.0f;
  while (exp) {
    if (exp & 1) {
      result *= base;
    }
    exp >>= 1;
    base *= base;
  }
  return result;
}

f32 math_exp_f32(const f32 exp) { return intrinsic_exp_f32(exp); }

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

i32 math_clamp_i32(const i32 val, const i32 min, const i32 max) {
  if (val <= min) {
    return min;
  }
  if (val >= max) {
    return max;
  }
  return val;
}

i64 math_clamp_i64(const i64 val, const i64 min, const i64 max) {
  if (val <= min) {
    return min;
  }
  if (val >= max) {
    return max;
  }
  return val;
}

f32 math_lerp_angle_f32(const f32 angleX, const f32 angleY, const f32 t) {
  const f32 diff         = math_mod_f32(angleY - angleX, math_pi_f32 * 2);
  const f32 shortestDiff = math_mod_f32(diff * 2, math_pi_f32 * 2) - diff;
  return angleX + shortestDiff * t;
}

bool math_towards_f32(f32* val, const f32 target, const f32 maxDelta) {
  if (math_abs(target - *val) <= maxDelta) {
    *val = target;
    return true;
  }
  *val += math_sign(target - *val) * maxDelta;
  return false;
}
