#pragma once
#include <immintrin.h>

// clang-format off

#define intrinsic_popcnt_64 _mm_popcnt_u64

#if defined(VOLO_MSVC)
#define intrinsic_ctz_64(_VAR_, _MASK_) u64 _VAR_; _BitScanForward64(&_VAR_, (_MASK_))
#else
#define intrinsic_ctz_64(_VAR_, _MASK_) u64 _VAR_ = __builtin_ctzll(_MASK_)
#endif

// clang-format on
