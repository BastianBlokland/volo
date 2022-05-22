#pragma once

#if defined(VOLO_MSVC)

#include <math.h>
#pragma intrinsic(atanf)
#pragma intrinsic(cosf)
#pragma intrinsic(sinf)
#pragma intrinsic(sqrtf)
#pragma intrinsic(tanf)

#define intrinsic_atan_f32 atanf
#define intrinsic_cos_f32 cosf
#define intrinsic_sin_f32 sinf
#define intrinsic_sqrt_f32 sqrtf
#define intrinsic_tan_f32 tanf

#else

#define intrinsic_atan_f32 __builtin_atanf
#define intrinsic_cos_f32 __builtin_cosf
#define intrinsic_sin_f32 __builtin_sinf
#define intrinsic_sqrt_f32 __builtin_sqrtf
#define intrinsic_tan_f32 __builtin_tanf

#endif
