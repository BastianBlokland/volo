#pragma once
#include "core_types.h"

#if defined(VOLO_CLANG) || defined(VOLO_GCC)
#define likely(_CONDITION_) __builtin_expect(!!(_CONDITION_), true)
#define unlikely(_CONDITION_) __builtin_expect(!!(_CONDITION_), false)
#else
#define likely(_CONDITION_) _CONDITION_
#define unlikely(_CONDITION_) _CONDITION_
#endif
