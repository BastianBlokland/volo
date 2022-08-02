#pragma once

#if defined(VOLO_MSVC)

#include <math.h>
#pragma intrinsic(atanf)
#pragma intrinsic(cosf)
#pragma intrinsic(acosf)
#pragma intrinsic(sinf)
#pragma intrinsic(asinf)
#pragma intrinsic(sqrtf)
#pragma intrinsic(tanf)
#pragma intrinsic(atan2f)
#pragma intrinsic(round)
#pragma intrinsic(roundf)

#define intrinsic_atan_f32 atanf
#define intrinsic_cos_f32 cosf
#define intrinsic_acos_f32 acosf
#define intrinsic_sin_f32 sinf
#define intrinsic_asin_f32 asinf
#define intrinsic_sqrt_f32 sqrtf
#define intrinsic_tan_f32 tanf
#define intrinsic_atan2_f32 atan2f
#define intrinsic_round_f64 round
#define intrinsic_round_f32 roundf

#else

#define intrinsic_atan_f32 __builtin_atanf
#define intrinsic_cos_f32 __builtin_cosf
#define intrinsic_acos_f32 __builtin_acosf
#define intrinsic_sin_f32 __builtin_sinf
#define intrinsic_asin_f32 __builtin_asinf
#define intrinsic_sqrt_f32 __builtin_sqrtf
#define intrinsic_tan_f32 __builtin_tanf
#define intrinsic_atan2_f32 __builtin_atan2f
#define intrinsic_round_f64 __builtin_round
#define intrinsic_round_f32 __builtin_roundf

#endif
