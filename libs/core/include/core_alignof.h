#pragma once
#include "core_types.h"

/**
 * Return the alignment required for the given type.
 */
#ifdef VOLO_CLANG
#define alignof(_TYPE_) ((u32) __alignof__(_TYPE_))
#elif defined(VOLO_GCC)
#define alignof(_TYPE_) ((u32) __alignof__(_TYPE_))
#elif defined(VOLO_MSVC)
#define alignof(_TYPE_) ((u32) __alignof(_TYPE_))
#else
_Static_assert(false, "Unsupported compiler");
#endif
