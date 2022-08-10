#pragma once

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
#pragma intrinsic(sinf)
#pragma intrinsic(sqrtf)
#pragma intrinsic(tanf)

#define intrinsic_acos_f32 acosf
#define intrinsic_asin_f32 asinf
#define intrinsic_atan_f32 atanf
#define intrinsic_atan2_f32 atan2f
#define intrinsic_cos_f32 cosf
#define intrinsic_round_down_f32 floorf
#define intrinsic_round_down_f64 floor
#define intrinsic_round_nearest_f32 roundf
#define intrinsic_round_nearest_f64 round
#define intrinsic_round_up_f32 ceilf
#define intrinsic_round_up_f64 ceil
#define intrinsic_sin_f32 sinf
#define intrinsic_sqrt_f32 sqrtf
#define intrinsic_tan_f32 tanf

#else

#define intrinsic_acos_f32 __builtin_acosf
#define intrinsic_asin_f32 __builtin_asinf
#define intrinsic_atan_f32 __builtin_atanf
#define intrinsic_atan2_f32 __builtin_atan2f
#define intrinsic_cos_f32 __builtin_cosf
#define intrinsic_round_down_f32 __builtin_floorf
#define intrinsic_round_down_f64 __builtin_floor
#define intrinsic_round_nearest_f32 __builtin_roundf
#define intrinsic_round_nearest_f64 __builtin_round
#define intrinsic_round_up_f32 __builtin_ceilf
#define intrinsic_round_up_f64 __builtin_ceil
#define intrinsic_sin_f32 __builtin_sinf
#define intrinsic_sqrt_f32 __builtin_sqrtf
#define intrinsic_tan_f32 __builtin_tanf

#endif
