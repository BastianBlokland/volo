#pragma once
#include "core_types.h"

#if defined(VOLO_CLANG) || defined(VOLO_GCC)
#define likely(x) __builtin_expect(!!(x), true)
#define unlikely(x) __builtin_expect(!!(x), false)
#else
#define likely(x) x
#define unlikely(x) x
#endif
