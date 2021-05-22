#pragma once

/**
 * Disable compiler optimizations. Usefull for debugging specific code in an otherwise optimized
 * translation unit.
 */
#if defined(VOLO_CLANG)
#define VOLO_OPTIMIZE_OFF _Pragma("clang optimize off")
#elif defined(VOLO_GCC)
#define VOLO_OPTIMIZE_OFF _Pragma("GCC optimize(\"-O0\")")
#elif defined(VOLO_MSVC)
#define VOLO_OPTIMIZE_OFF _Pragma("optimize(\"\", off)")
#else
#define VOLO_OPTIMIZE_OFF
#endif

/**
 * Compiler hints to indicate if a branch is likely or unlikely to be taken. Helps the compiler
 * determine which parts of the code are hot vs cold.
 */
#if defined(VOLO_CLANG) || defined(VOLO_GCC)
#define likely(_CONDITION_) __builtin_expect(!!(_CONDITION_), true)
#define unlikely(_CONDITION_) __builtin_expect(!!(_CONDITION_), false)
#else
#define likely(_CONDITION_) _CONDITION_
#define unlikely(_CONDITION_) _CONDITION_
#endif
