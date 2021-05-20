#pragma once
#include "core_types.h"

/**
 * Non-owning view over memory.
 */
typedef struct {
  void* ptr;
  usize size;
} Mem;

#define mem_valid(_MEM_) ((_MEM_).ptr != null)
#define mem_itr(_MEM_) ((u8*)(_MEM_).ptr)
#define mem_end(_MEM_) ((u8*)(_MEM_).ptr + (_MEM_).size)
#define mem_as_t(_MEM_, _TYPE_) ((_TYPE_*)mem_as(_MEM_, sizeof(_TYPE_)))

#define mem_for_u8(_MEM_, _VAR_, ...)                                                              \
  {                                                                                                \
    const u8* _VAR_##_end = mem_end(_MEM_);                                                        \
    for (u8* _VAR_##_itr = mem_itr(_MEM_); _VAR_##_itr != _VAR_##_end; ++_VAR_##_itr) {            \
      const u8 _VAR_ = *_VAR_##_itr;                                                               \
      __VA_ARGS__                                                                                  \
    }                                                                                              \
  }

void  mem_set(Mem, u8 val);
void  mem_cpy(Mem dst, Mem src);
void  mem_move(Mem dst, Mem src);
Mem   mem_slice(Mem, usize offset, usize size);
void* mem_as(Mem mem, usize size);
i32   mem_cmp(Mem a, Mem b);
bool  mem_eq(Mem a, Mem b);
