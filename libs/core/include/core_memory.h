#pragma once
#include "core_types.h"

typedef struct {
  void* ptr;
  usize size;
} Mem;

#define mem_valid(mem) (mem.ptr != null)
#define mem_itr(mem) ((char*)mem.ptr)
#define mem_end(mem) ((char*)mem.ptr + mem.size)
#define mem_as_t(mem, type) ((type*)mem_as(mem, sizeof(type)))

void  mem_set(Mem, u8 val);
void  mem_cpy(Mem dst, Mem src);
void  mem_move(Mem dst, Mem src);
Mem   mem_slice(Mem, usize offset, usize size);
void* mem_as(Mem mem, usize size);

/**
 * Memory management utilities.
 * TODO: Move these to an allocator api.
 */
Mem  mem_alloc(usize size);
void mem_free(Mem);
