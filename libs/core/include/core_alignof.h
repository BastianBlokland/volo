#pragma once
#include "core_annotation.h"

/**
 * Return the alignment required for the given type.
 */
#if defined(VOLO_CLANG)
#define alignof(_TYPE_) __alignof__(_TYPE_)
#elif defined(VOLO_GCC)
#define alignof(_TYPE_) __alignof__(_TYPE_)
#elif defined(VOLO_MSVC)
#define alignof(_TYPE_) __alignof(_TYPE_)
#else
ASSERT(false, "Unsupported compiler");
#endif
