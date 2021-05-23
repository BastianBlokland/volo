#pragma once
#include "core_types.h"

/**
 * Non-owning view over memory with a specified size.
 */
typedef struct {
  void* ptr;
  usize size;
} Mem;

/**
 * Create a view over the given memory.
 * Note: The memory view is only valid as long as the underlying memory remains valid.
 */
#define mem_create(_PTR_, _SIZE_)                                                                  \
  ((Mem){                                                                                          \
      .ptr  = (void*)(_PTR_),                                                                      \
      .size = _SIZE_,                                                                              \
  })

/**
 * Check if the memory view is valid.
 * Note: Only checks if it was initialized properly, does NOT check if there is actually memory
 * backing it.
 */
#define mem_valid(_MEM_) ((_MEM_).ptr != null)

/**
 * Retrieve a u8 pointer to the start of the memory.
 */
#define mem_itr(_MEM_) ((u8*)(_MEM_).ptr)

/**
 * Retrieve a u8 pointer to the end of the memory (1 past the last valid byte).
 */
#define mem_end(_MEM_) ((u8*)(_MEM_).ptr + (_MEM_).size)

/**
 * Retrieve a u8 pointer to a specific byte.
 * Pre-condition: '_IDX_' < memory.size
 */
#define mem_at_u8(_MEM_, _IDX_) ((u8*)(_MEM_).ptr + (_IDX_))

/**
 * Interpret this memory as type '_TYPE_'.
 * Pre-condition: sizeof(_TYPE_) <= mem.size
 */
#define mem_as_t(_MEM_, _TYPE_) ((_TYPE_*)mem_as(_MEM_, sizeof(_TYPE_)))

/**
 * Iterate over each byte.
 */
#define mem_for_u8(_MEM_, _VAR_, ...)                                                              \
  {                                                                                                \
    const u8* _VAR_##_end = mem_end(_MEM_);                                                        \
    for (u8* _VAR_##_itr = mem_itr(_MEM_); _VAR_##_itr != _VAR_##_end; ++_VAR_##_itr) {            \
      const u8 _VAR_ = *_VAR_##_itr;                                                               \
      __VA_ARGS__                                                                                  \
    }                                                                                              \
  }

/**
 * Set each byte equal to the given value.
 */
void mem_set(Mem, u8 val);

/**
 * Copy all bytes from 'src' to 'dst'. Does NOT support overlapping memory views.
 * Pre-condition: dst.size >= src.size
 * Pre-condition: src and dst do NOT overlap.
 */
void mem_cpy(Mem dst, Mem src);

/**
 * Copy all bytes from 'src' to 'dst'. Supports overlapping memory views.
 * Pre-condition: dst.size >= src.size
 */
void mem_move(Mem dst, Mem src);

/**
 * Create a view to a sub-section of this memory.
 * Pre-condition: mem.size >= offset + size
 */
Mem mem_slice(Mem, usize offset, usize size);

/**
 * Interpret this memory as an object with the given size.
 * Only performs diagnostic size validation, should be considered a no-op for non-debug builds.
 */
void* mem_as(Mem mem, usize size);

/**
 * Compare memory a and b.
 * If a.size < b.size then -1 is returned.
 * If a.size > b.size then 1 is returned.
 * Otherwise it will compare a and b byte-wise and return -1, 0 or 1.
 */
i32 mem_cmp(Mem a, Mem b);

/**
 * Check if all bytes in memory a and b are equal.s
 */
bool mem_eq(Mem a, Mem b);
