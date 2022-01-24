#include "core_bits.h"
#include "core_diag.h"
#include "core_math.h"
#include "core_memory.h"

#if defined(VOLO_MSVC)

#include <string.h>
#pragma intrinsic(memset)
#pragma intrinsic(memcpy)
#pragma intrinsic(memmove)
#pragma intrinsic(memcmp)

#else

#define memset __builtin_memset
#define memcpy __builtin_memcpy
#define memmove __builtin_memmove
#define memcmp __builtin_memcmp

#endif

void mem_set(const Mem mem, const u8 val) {
  diag_assert(mem_valid(mem));
  memset(mem.ptr, val, mem.size);
}

void mem_cpy(const Mem dst, const Mem src) {
  diag_assert(!src.size || mem_valid(dst));
  diag_assert(!src.size || mem_valid(src));
  diag_assert(dst.size >= src.size);
  memcpy(dst.ptr, src.ptr, src.size);
}

void mem_move(const Mem dst, const Mem src) {
  diag_assert(mem_valid(dst));
  diag_assert(mem_valid(src));
  diag_assert(dst.size >= src.size);
  memmove(dst.ptr, src.ptr, src.size);
}

Mem mem_slice(const Mem mem, const usize offset, const usize size) {
  diag_assert(!size || mem_valid(mem));
  diag_assert(mem.size >= offset + size);
  return mem_create(bits_ptr_offset(mem.ptr, offset), size);
}

Mem mem_consume(const Mem mem, const usize amount) {
  diag_assert(mem.size >= amount);
  return (Mem){
      .ptr  = bits_ptr_offset(mem.ptr, amount),
      .size = mem.size - amount,
  };
}

Mem mem_consume_u8(const Mem mem, u8* out) {
  diag_assert(mem.size >= 1);
  *out = *mem_begin(mem);
  return mem_consume(mem, 1);
}

Mem mem_consume_le_u16(const Mem mem, u16* out) {
  diag_assert(mem.size >= 2);
  u8* data = mem_begin(mem);
  *out     = (u16)data[0] | (u16)data[1] << 8;
  return mem_consume(mem, 2);
}

Mem mem_consume_le_u32(const Mem mem, u32* out) {
  diag_assert(mem.size >= 4);
  u8* data = mem_begin(mem);
  *out     = (u32)data[0] | (u32)data[1] << 8 | (u32)data[2] << 16 | (u32)data[3] << 24;
  return mem_consume(mem, 4);
}

Mem mem_consume_le_u64(const Mem mem, u64* out) {
  diag_assert(mem.size >= 8);
  u8* data = mem_begin(mem);
  *out =
      ((u64)data[0] | (u64)data[1] << 8 | (u64)data[2] << 16 | (u64)data[3] << 24 |
       (u64)data[4] << 32 | (u64)data[5] << 40 | (u64)data[6] << 48 | (u64)data[7] << 56);
  return mem_consume(mem, 8);
}

Mem mem_consume_be_u16(const Mem mem, u16* out) {
  diag_assert(mem.size >= 2);
  u8* data = mem_begin(mem);
  *out     = (u16)data[0] << 8 | (u16)data[1];
  return mem_consume(mem, 2);
}

Mem mem_consume_be_u32(const Mem mem, u32* out) {
  diag_assert(mem.size >= 4);
  u8* data = mem_begin(mem);
  *out     = (u32)data[0] << 24 | (u32)data[1] << 16 | (u32)data[2] << 8 | (u32)data[3];
  return mem_consume(mem, 4);
}

Mem mem_consume_be_u64(const Mem mem, u64* out) {
  diag_assert(mem.size >= 8);
  u8* data = mem_begin(mem);
  *out =
      ((u64)data[0] << 56 | (u64)data[1] << 48 | (u64)data[2] << 40 | (u64)data[3] << 32 |
       (u64)data[4] << 24 | (u64)data[5] << 16 | (u64)data[6] << 8 | (u64)data[7]);
  return mem_consume(mem, 8);
}

void* mem_as(const Mem mem, const usize size, const usize align) {
  (void)size;
  (void)align;

  diag_assert(mem_valid(mem));
  diag_assert(mem.size >= size);
  diag_assert(bits_aligned_ptr(mem.ptr, align));
  return mem.ptr;
}

i8 mem_cmp(const Mem a, const Mem b) {
  diag_assert(mem_valid(a));
  diag_assert(mem_valid(b));
  const int cmp = memcmp(a.ptr, b.ptr, math_min(a.size, b.size));
  return math_sign(cmp);
}

bool mem_eq(const Mem a, const Mem b) {
  diag_assert(!a.size || mem_valid(a));
  diag_assert(!b.size || mem_valid(b));
  return a.size == b.size && memcmp(a.ptr, b.ptr, a.size) == 0;
}

bool mem_contains(const Mem mem, const u8 byte) {
  mem_for_u8(mem, itr) {
    if (*itr == byte) {
      return true;
    }
  }
  return false;
}

bool mem_all(Mem mem, const u8 byte) {
  mem_for_u8(mem, itr) {
    if (*itr != byte) {
      return false;
    }
  }
  return true;
}

void mem_swap(const Mem a, const Mem b) {
  diag_assert(mem_valid(a));
  diag_assert(mem_valid(b));
  diag_assert(a.size == b.size);

  mem_swap_raw(a.ptr, b.ptr, (u16)a.size);
}

void mem_swap_raw(void* a, void* b, const u16 size) {
  diag_assert(size <= 1024);

  Mem buffer = mem_stack(size);
  memcpy(buffer.ptr, a, size);
  memcpy(a, b, size);
  memcpy(b, buffer.ptr, size);
}
