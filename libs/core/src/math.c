#include "core_array.h"
#include "core_diag.h"
#include "core_math.h"

#if defined(VOLO_MSVC)

#include <math.h>
#pragma intrinsic(acosf)
#pragma intrinsic(asinf)
#pragma intrinsic(atan2f)
#pragma intrinsic(atanf)
#pragma intrinsic(ceil)
#pragma intrinsic(ceilf)
#pragma intrinsic(cosf)
#pragma intrinsic(floor)
#pragma intrinsic(floorf)
#pragma intrinsic(fmodf)
#pragma intrinsic(logf)
#pragma intrinsic(powf)
#pragma intrinsic(sinf)
#pragma intrinsic(sqrt)
#pragma intrinsic(sqrtf)
#pragma intrinsic(tanf)

#else

#define acosf __builtin_acosf
#define asinf __builtin_asinf
#define atan2f __builtin_atan2f
#define atanf __builtin_atanf
#define ceil __builtin_ceil
#define ceilf __builtin_ceilf
#define cosf __builtin_cosf
#define floor __builtin_floor
#define floorf __builtin_floorf
#define fmodf __builtin_fmodf
#define logf __builtin_logf
#define powf __builtin_powf
#define round __builtin_round
#define roundf __builtin_roundf
#define sinf __builtin_sinf
#define sqrt __builtin_sqrt
#define sqrtf __builtin_sqrtf
#define tanf __builtin_tanf

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

f32 math_mod_f32(const f32 x, const f32 y) { return fmodf(x, y); }

f32 math_sqrt_f32(const f32 val) { return sqrtf(val); }
f64 math_sqrt_f64(const f64 val) { return sqrt(val); }

f32 math_log_f32(const f32 val) { return logf(val); }

f32 math_sin_f32(const f32 val) { return sinf(val); }

f32 math_asin_f32(const f32 val) { return asinf(val); }

f32 math_cos_f32(const f32 val) { return cosf(val); }

f32 math_acos_f32(const f32 val) { return acosf(val); }

f32 math_tan_f32(const f32 val) { return tanf(val); }

f32 math_atan_f32(const f32 val) { return atanf(val); }

f32 math_atan2_f32(const f32 x, const f32 y) { return atan2f(x, y); }

f32 math_pow_f32(const f32 base, const f32 exp) { return powf(base, exp); }

f32 math_trunc_f32(const f32 val) { return (i32)val; }
f64 math_trunc_f64(const f64 val) { return (i64)val; }

f32 math_floor_f32(const f32 val) { return floorf(val); }
f64 math_floor_f64(const f64 val) { return floor(val); }

f32 math_ceil_f32(const f32 val) { return ceilf(val); }
f64 math_ceil_f64(const f64 val) { return ceil(val); }

f32 math_round_f32(const f32 val) { return roundf(val); }
f64 math_round_f64(const f64 val) { return round(val); }

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
