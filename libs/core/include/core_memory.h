#pragma once
#include "core_types.h"

typedef struct {
  void* ptr;
  usize size;
} Mem;

#define mem_valid(_MEM_) ((_MEM_).ptr != null)
#define mem_itr(_MEM_) ((char*)(_MEM_).ptr)
#define mem_end(_MEM_) ((char*)(_MEM_).ptr + (_MEM_).size)
#define mem_as_t(_MEM_, _TYPE_) ((_TYPE_*)mem_as(_MEM_, sizeof(_TYPE_)))

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
