#pragma once

/**
 * Compile-time assert the given condition.
 */
#define ASSERT(_CONDITION_, _MSG_LIT_) _Static_assert(_CONDITION_, _MSG_LIT_);

/**
 * Compiler hints to indicate if a branch is likely or unlikely to be taken. Helps the compiler
 * determine which parts of the code are hot vs cold.
 */
#if defined(VOLO_CLANG) || defined(VOLO_GCC)
#define LIKELY(_CONDITION_) __builtin_expect(!!(_CONDITION_), true)
#define UNLIKELY(_CONDITION_) __builtin_expect(!!(_CONDITION_), false)
#else
#define LIKELY(_CONDITION_) _CONDITION_
#define UNLIKELY(_CONDITION_) _CONDITION_
#endif

/**
 * Disable compiler optimizations. Usefull for debugging specific code in an otherwise optimized
 * translation unit.
 */
#if defined(VOLO_CLANG)
#define OPTIMIZE_OFF() _Pragma("clang optimize off")
#elif defined(VOLO_GCC)
#define OPTIMIZE_OFF() _Pragma("GCC optimize(\"-O0\")")
#elif defined(VOLO_MSVC)
#define OPTIMIZE_OFF() _Pragma("optimize(\"\", off)")
#else
#define OPTIMIZE_OFF()
#endif

/**
 * Indicates to the compiler that this function does not return.
 */
#define NORETURN _Noreturn

/**
 * Indicates that this code-path cannot be reached.
 */
#if defined(VOLO_CLANG) || defined(VOLO_GCC)
#define UNREACHABLE __builtin_unreachable();
#elif defined(VOLO_MSVC)
#define UNREACHABLE __assume(false);
#else
ASSERT(false, "Unsupported compiler");
#endif

/**
 * Hint to the compiler that its okay for a variable or function to be unused.
 */
#if defined(VOLO_CLANG) || defined(VOLO_GCC)
#define MAYBE_UNUSED __attribute__((unused))
#elif defined(VOLO_MSVC)
#define MAYBE_UNUSED __pragma(warning(suppress : 4100 4101))
#else
#define MAYBE_UNUSED
#endif

/**
 * Hint to the compiler that this function should be inlined.
 */
#if defined(VOLO_CLANG) || defined(VOLO_GCC)
#define INLINE_HINT __attribute__((always_inline)) inline
#elif defined(VOLO_MSVC)
#define INLINE_HINT __forceinline
#else
#define INLINE_HINT
#endif

/**
 * Hint to the compiler that this function should not be inlined.
 */
#if defined(VOLO_CLANG) || defined(VOLO_GCC)
#define NO_INLINE_HINT __attribute__((noinline))
#elif defined(VOLO_MSVC)
#define NO_INLINE_HINT __declspec(noinline)
#else
#define NO_INLINE_HINT
#endif

/**
 * Hint to the compiler that all function calls should be inlined into this function.
 */
#if defined(VOLO_CLANG) || defined(VOLO_GCC)
#define FLATTEN_HINT __attribute__((flatten))
#else
#define FLATTEN_HINT
#endif

/**
 * Mark a variable as having thread storage duration.
 * Which means it is created when the thread starts and cleaned up when the thread ends.
 */
#if defined(VOLO_CLANG) || defined(VOLO_GCC)
#define THREAD_LOCAL __thread
#elif defined(VOLO_MSVC)
#define THREAD_LOCAL __declspec(thread)
#else
ASSERT(false, "Unsupported compiler");
#endif

/**
 * Mark a structure or enum to be packed, meaning it will use as little memory as possible.
 * NOTE: Behaviour differs per compiler, MSVC does not support this on enums at all for example.
 *
 * Example usage:
 * '
 *   PACKED(typedef enum {
 *     MyEnum_A,
 *     MyEnum_B,
 *   }) MyEnum;
 * '
 * '
 *   PACKED(typedef struct {
 *     i32 a;
 *     f32 b;
 *   }) MyStruct;
 * '
 */
#if defined(VOLO_CLANG) || defined(VOLO_GCC)
#define PACKED(...) __VA_ARGS__ __attribute__((__packed__))
#elif defined(VOLO_MSVC)
#define PACKED(...) __pragma(pack(push, 1)) __VA_ARGS__ __pragma(pack(pop))
#else
#define PACKED(...) __VA_ARGS__
#endif

/**
 * Specify a specific alignment for a field instead of its natural alignment.
 *
 * Example usage:
 * ```
 * struct MyStruct {
 *   u32 fieldA;
 *   ALIGNAS(8) u32 fieldB;
 * }
 * ```
 */
#define ALIGNAS(_ALIGNMENT_) _Alignas(_ALIGNMENT_)
