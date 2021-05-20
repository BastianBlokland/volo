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

typedef u8 bool;
#define true 1
#define false 0

#define null 0

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

#define i8_sentinel INT8_MAX
#define i16_sentinel INT16_MAX
#define i32_sentinel INT32_MAX
#define i64_sentinel INT64_MAX
#define u8_sentinel UINT8_MAX
#define u16_sentinel UINT16_MAX
#define u32_sentinel UINT32_MAX
#define u64_sentinel UINT64_MAX
#define usize_sentinel SIZE_MAX
#define iptr_sentinel INTPTR_MAX
#define uptr_sentinel UINTPTR_MAX
