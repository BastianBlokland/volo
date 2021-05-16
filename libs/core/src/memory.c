#include "core_diag.h"
#include "core_memory.h"
#include <stdlib.h>
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
  return (Mem){
      .ptr  = mem.ptr + offset,
      .size = size,
  };
}

void* mem_as(Mem mem, const usize size) {
  diag_assert(mem_valid(mem));
  diag_assert(mem.size >= size);
  return mem.ptr;
}

Mem mem_alloc(const usize size) {
  diag_assert(size);
  return (Mem){
      .ptr  = malloc(size),
      .size = size,
  };
}

void mem_free(Mem mem) {
  diag_assert(mem_valid(mem));
  mem_set(mem, 0xFF); // Basic tag to detect use-after-free.
  free(mem.ptr);
}
