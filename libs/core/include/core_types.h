#pragma once
#include <stddef.h>
#include <stdint.h>

typedef int8_t    i8;
typedef int16_t   i16;
typedef int32_t   i32;
typedef int64_t   i64;
typedef uint8_t   u8;
typedef uint16_t  u16;
typedef uint32_t  u32;
typedef uint64_t  u64;
typedef size_t    usize;
typedef intptr_t  iptr;
typedef uintptr_t uptr;

typedef float  f32;
typedef double f64;

typedef u8 bool;
#define true 1
#define false 0

#define null 0

#define u8_lit(_LITERAL_) UINT8_C(_LITERAL_)
#define i8_lit(_LITERAL_) INT8_C(_LITERAL_)
#define u16_lit(_LITERAL_) UINT16_C(_LITERAL_)
#define i16_lit(_LITERAL_) INT16_C(_LITERAL_)
#define u32_lit(_LITERAL_) UINT32_C(_LITERAL_)
#define i32_lit(_LITERAL_) INT32_C(_LITERAL_)
#define u64_lit(_LITERAL_) UINT64_C(_LITERAL_)
#define i64_lit(_LITERAL_) INT64_C(_LITERAL_)
#define usize_lit(_LITERAL_) ((usize)UINT64_C(_LITERAL_))

#define i8_min INT8_MIN
#define i16_min INT16_MIN
#define i32_min INT32_MIN
#define i64_min INT64_MIN
#define iptr_min INTPTR_MIN

#define i8_max INT8_MAX
#define i16_max INT16_MAX
#define i32_max INT32_MAX
#define i64_max INT64_MAX
#define u8_max UINT8_MAX
#define u16_max UINT16_MAX
#define u32_max UINT32_MAX
#define u64_max UINT64_MAX
#define usize_max SIZE_MAX
#define iptr_max INTPTR_MAX
#define uptr_max UINTPTR_MAX

#define usize_byte ((usize)1)
#define usize_kibibyte (usize_byte * 1024)
#define usize_mebibyte (usize_kibibyte * 1024)
#define usize_gibibyte (usize_mebibyte * 1024)
#define usize_tebibyte (usize_gibibyte * 1024)
#define usize_pebibyte (usize_tebibyte * 1024)

/**
 * Return the alignment required for the given type.
 */
#if defined(VOLO_MSVC)
#define alignof(_TYPE_) __alignof(_TYPE_)
#else
#define alignof(_TYPE_) __alignof__(_TYPE_)
#endif

/**
 * Retrieve a pointer to a field inside the given value.
 * Example usage:
 * ```
 * void* myVal = ...
 * void* myValField = field_ptr(myVal, MyStructType, fieldA);
 * ```
 */
#define field_ptr(_VAL_, _TYPE_, _FIELD_) (&((_TYPE_*)(_VAL_))->_FIELD_)
