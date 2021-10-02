#pragma once
#include "core_types.h"

/**
 * Non-owning view over a memory block.
 */
typedef struct {
  void* ptr;
  usize size;
} Mem;

/**
 * Zero sized memory block.
 */
#define mem_empty ((Mem){0})

/**
 * Create a memory view over a stack allocated struct.
 */
#define mem_struct(_TYPE_, ...)                                                                    \
  ((Mem){                                                                                          \
      .ptr  = &(_TYPE_){0, ##__VA_ARGS__},                                                         \
      .size = sizeof(_TYPE_),                                                                      \
  })

/**
 * Create a view over the given memory.
 * NOTE: The memory view is only valid as long as the underlying memory remains valid.
 */
#define mem_create(_PTR_, _SIZE_)                                                                  \
  ((Mem){                                                                                          \
      .ptr  = (void*)(_PTR_),                                                                      \
      .size = (_SIZE_),                                                                            \
  })

/**
 * Create a view over the given memory.
 * NOTE: _BEGIN_ is expanded multiple times, so care must be taken when providing complex
 * expressions.
 * NOTE: The memory view is only valid as long as the underlying memory remains valid.
 */
#define mem_from_to(_BEGIN_, _END_)                                                                \
  ((Mem){                                                                                          \
      .ptr  = (void*)(_BEGIN_),                                                                    \
      .size = (u8*)(_END_) - (u8*)(_BEGIN_),                                                       \
  })

/**
 * Check if the memory view is valid.
 * NOTE: Only checks if it was initialized properly, does NOT check if there is actually memory
 * backing it.
 */
#define mem_valid(_MEM_) ((_MEM_).ptr != null)

/**
 * Retrieve a u8 pointer to the start of the memory.
 */
#define mem_begin(_MEM_) ((u8*)(_MEM_).ptr)

/**
 * Retrieve a u8 pointer to the end of the memory (1 past the last valid byte).
 * NOTE: _MEM_ is expanded multiple times, so care must be taken when providing complex expressions.
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
#define mem_as_t(_MEM_, _TYPE_) ((_TYPE_*)mem_as(_MEM_, sizeof(_TYPE_), alignof(_TYPE_)))

/**
 * Iterate over each byte.
 * NOTE: _MEM_ is expanded multiple times, so care must be taken when providing complex expressions.
 */
#define mem_for_u8(_MEM_, _VAR_, ...)                                                              \
  {                                                                                                \
    const u8* _VAR_##_end = mem_end(_MEM_);                                                        \
    for (u8* _VAR_##_itr = mem_begin(_MEM_); _VAR_##_itr != _VAR_##_end; ++_VAR_##_itr) {          \
      const u8 _VAR_ = *_VAR_##_itr;                                                               \
      __VA_ARGS__                                                                                  \
    }                                                                                              \
  }

/**
 * Create a memory buffer on the stack.
 * NOTE: Care must be taken not to overflow the stack by using too high _SIZE_ values.
 * NOTE: _SIZE_ is expanded multiple times, so care must be taken when providing complex
 * expressions.
 */
#if defined(VOLO_MSVC)
#define mem_stack(_SIZE_) mem_create(_alloca(_SIZE_), _SIZE_)
#else
#define mem_stack(_SIZE_) mem_create(__builtin_alloca(_SIZE_), _SIZE_)
#endif

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
 * Create a view 'amount' bytes into the memory.
 * Pre-condition: size >= amount.
 */
Mem mem_consume(Mem, usize amount);

/**
 * Interpret this memory as an object with the given size.
 * Only performs diagnostic size / align validation, no-op in non-debug builds.
 */
void* mem_as(Mem mem, usize size, usize align);

/**
 * Compare memory a and b.
 * Byte-wise compare and return -1, 0 or 1.
 */
i8 mem_cmp(Mem a, Mem b);

/**
 * Check if all bytes in memory a and b are equal.s
 */
bool mem_eq(Mem a, Mem b);

/**
 * Check if the given memory region contains a specific byte.
 */
bool mem_contains(Mem, u8 byte);

/**
 * Swap the memory contents.
 * Pre-condition: a.size == b.size
 * Pre-condition: a.size <= 1024.
 */
void mem_swap(Mem a, Mem b);

/**
 * Swap the memory contents.
 * Pre-condition: size <= 1024.
 */
void mem_swap_raw(void* a, void* b, const u16 size);
