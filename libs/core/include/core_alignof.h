#pragma once
#include "core_types.h"

#ifdef VOLO_CLANG
#define alignof(type) ((u32) __alignof__(type))
#elif defined(VOLO_GCC)
#define alignof(type) ((u32) __alignof__(type))
#elif defined(VOLO_MSVC)
#define alignof(type) ((u32) __alignof(type))
#else
_Static_assert(false, "Unknown compiler");
#endif
