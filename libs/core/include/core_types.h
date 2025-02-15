#pragma once

/**
 * Primitive type definitions.
 */

typedef signed char        i8;
typedef signed short int   i16;
typedef signed int         i32;
typedef signed long int    i64;
typedef unsigned char      u8;
typedef unsigned short int u16;
typedef unsigned int       u32;
typedef unsigned long int  u64;

#ifdef VOLO_32_BIT
typedef unsigned int usize;
typedef signed int   iptr;
typedef unsigned int uptr;
#else
typedef unsigned long int usize;
typedef signed long int   iptr;
typedef unsigned long int uptr;
#endif

typedef unsigned short int f16;
typedef float              f32;
typedef double             f64;

typedef unsigned char bool;
#define true 1
#define false 0

#define null ((void*)0)

#define u8_lit(_LITERAL_) _LITERAL_
#define i8_lit(_LITERAL_) _LITERAL_
#define u16_lit(_LITERAL_) _LITERAL_
#define i16_lit(_LITERAL_) _LITERAL_
#define u32_lit(_LITERAL_) _LITERAL_##U
#define i32_lit(_LITERAL_) _LITERAL_
#define u64_lit(_LITERAL_) _LITERAL_##UL
#define i64_lit(_LITERAL_) _LITERAL_##L

#ifdef VOLO_32_BIT
#define usize_lit(_LITERAL_) _LITERAL_##U
#else
#define usize_lit(_LITERAL_) _LITERAL_##UL
#endif

#define i8_min (-128)
#define i16_min (-32767 - 1)
#define i32_min (-2147483647 - 1)
#define i64_min (-i64_lit(9223372036854775807) - 1)

#ifdef VOLO_32_BIT
#define iptr_min (-2147483647 - 1)
#else
#define iptr_min (-i64_lit(9223372036854775807) - 1)
#endif

#define i8_max (127)
#define i16_max (32767)
#define i32_max (2147483647)
#define i64_max (i64_lit(9223372036854775807))
#define u8_max (255)
#define u16_max (65535)
#define u32_max (4294967295U)
#define u64_max (u64_lit(18446744073709551615))

#ifdef VOLO_32_BIT
#define usize_max (4294967295U)
#define iptr_max (2147483647)
#define uptr_max (4294967295U)
#else
#define usize_max (u64_lit(18446744073709551615))
#define iptr_max (i64_lit(9223372036854775807))
#define uptr_max (u64_lit(18446744073709551615))
#endif

#define usize_byte ((usize)1)
#define usize_kibibyte (usize_byte * 1024)
#define usize_mebibyte (usize_kibibyte * 1024)
#define usize_gibibyte (usize_mebibyte * 1024)
#define usize_tebibyte (usize_gibibyte * 1024)
#define usize_pebibyte (usize_tebibyte * 1024)

/**
 * Retrieve the offset (in bytes) of the given member in the type.
 */
#define offsetof(_TYPE_, __MEMBER__) __builtin_offsetof(_TYPE_, __MEMBER__)

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
