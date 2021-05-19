#pragma once
#include "core_diag.h"
#include "core_types.h"

#ifdef VOLO_CLANG
#define alignof(_TYPE_) ((u32) __alignof__(_TYPE_))
#elif defined(VOLO_GCC)
#define alignof(_TYPE_) ((u32) __alignof__(_TYPE_))
#elif defined(VOLO_MSVC)
#define alignof(_TYPE_) ((u32) __alignof(_TYPE_))
#else
diag_static_assert(false, "Unknown compiler");
#endif
