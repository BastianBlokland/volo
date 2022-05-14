#pragma once

#if defined(VOLO_MSVC)

#include <math.h>
#pragma intrinsic(cosf)
#pragma intrinsic(sinf)

#define intrinsic_cosf cosf
#define intrinsic_sinf sinf

#else

#define intrinsic_cosf __builtin_cosf
#define intrinsic_sinf __builtin_sinf

#endif
