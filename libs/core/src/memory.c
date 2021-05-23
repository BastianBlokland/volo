#include "core_diag.h"
#include "core_memory.h"
#include <string.h>

void mem_set(const Mem mem, const u8 val) {
  diag_assert(mem_valid(mem));
  memset(mem.ptr, val, mem.size);
}

void mem_cpy(const Mem dst, const Mem src) {
  diag_assert(mem_valid(dst));
  diag_assert(mem_valid(src));
  diag_assert(dst.size >= src.size);
  memcpy(dst.ptr, src.ptr, src.size);
}

void mem_move(const Mem dst, const Mem src) {
  diag_assert(mem_valid(dst));
  diag_assert(mem_valid(src));
  diag_assert(dst.size >= src.size);
  memmove(dst.ptr, src.ptr, src.size);
}

Mem mem_slice(Mem mem, const usize offset, const usize size) {
  diag_assert(mem_valid(mem));
  diag_assert(mem.size >= offset + size);
  return mem_create(mem.ptr + offset, size);
}

void* mem_as(Mem mem, const usize size) {
  diag_assert(mem_valid(mem));
  diag_assert(mem.size >= size);
  return mem.ptr;
}

i32 mem_cmp(Mem a, Mem b) {
  diag_assert(mem_valid(a));
  diag_assert(mem_valid(b));
  if (a.size < b.size) {
    return -1;
  }
  if (a.size > b.size) {
    return 1;
  }
  return memcmp(a.ptr, b.ptr, a.size);
}

bool mem_eq(Mem a, Mem b) {
  diag_assert(mem_valid(a));
  diag_assert(mem_valid(b));
  return a.size == b.size && memcmp(a.ptr, b.ptr, a.size) == 0;
}
