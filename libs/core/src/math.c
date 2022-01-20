#include "core_array.h"
#include "core_diag.h"
#include "core_math.h"

#if defined(VOLO_MSVC)

#include <math.h>
#pragma intrinsic(sqrtf)
#pragma intrinsic(logf)
#pragma intrinsic(sinf)
#pragma intrinsic(asinf)
#pragma intrinsic(cosf)
#pragma intrinsic(acosf)
#pragma intrinsic(tanf)
#pragma intrinsic(atanf)
#pragma intrinsic(atan2f)
#pragma intrinsic(powf)

#else

#define sqrtf __builtin_sqrtf
#define logf __builtin_logf
#define sinf __builtin_sinf
#define asinf __builtin_asinf
#define cosf __builtin_cosf
#define acosf __builtin_acosf
#define tanf __builtin_tanf
#define atanf __builtin_atanf
#define atan2f __builtin_atan2f
#define powf __builtin_powf

#endif

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

f32 math_sqrt_f32(const f32 val) { return sqrtf(val); }

f32 math_log_f32(const f32 val) { return logf(val); }

f32 math_sin_f32(const f32 val) { return sinf(val); }

f32 math_asin_f32(const f32 val) { return asinf(val); }

f32 math_cos_f32(const f32 val) { return cosf(val); }

f32 math_acos_f32(const f32 val) { return acosf(val); }

f32 math_tan_f32(const f32 val) { return tanf(val); }

f32 math_atan_f32(const f32 val) { return atanf(val); }

f32 math_atan2_f32(const f32 x, const f32 y) { return atan2f(x, y); }

f32 math_pow_f32(const f32 base, const f32 exp) { return powf(base, exp); }

f64 math_trunc_f64(const f64 val) { return (i64)val; }

f64 math_floor_f64(const f64 val) {
  const f64 trunc = math_trunc_f64(val);
  return trunc > val ? (trunc - 1) : trunc;
}

f64 math_ceil_f64(const f64 val) {
  const f64 trunc = math_trunc_f64(val);
  return trunc < val ? (trunc + 1) : trunc;
}

f64 math_round_f64(const f64 val) {
  const f64 trunc = math_trunc_f64(val);
  const f64 frac  = math_abs(val - trunc);
  if (frac < 0.5) {
    return trunc;
  }
  if (UNLIKELY(frac == 0.5)) {
    /**
     * Round-to-even (aka bankers rounding).
     * Given a number exactly halfway between two values, round to the even value (zero is
     * considered even here).
     */
    return ((i64)trunc % 2) ? (trunc + math_sign(val)) : trunc;
  }
  return trunc + math_sign(val);
}

f32 math_clamp_f32(const f32 val, const f32 min, const f32 max) {
  if (val <= min) {
    return min;
  }
  if (val >= max) {
    return max;
  }
  return val;
}
