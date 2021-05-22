#pragma once
#include "core_array.h"
#include "core_memory.h"

/**
 * Create a bump allocator backed by a buffer on the stack, a pointer to the allocator will be
 * available under the name '_VAR_'. Allocations will fail once the buffer has been filled up.
 * Note: Neither the allocator nor any allocations made from it are valid after the allocator goes
 * out of scope.
 * Note: Care must be taken not to overflow the stack by using too high _SIZE_ values.
 */
#define alloc_bump_create_stack(_VAR_, _SIZE_)                                                     \
  u8         _VAR_##_buffer[_SIZE_];                                                               \
  Allocator* _VAR_ = alloc_bump_create(array_mem(_VAR_##_buffer))

/**
 * Allocator handle.
 */
typedef struct sAllocator Allocator;

/**
 * 'Normal' heap allocator, semantics similar to 'malloc'.
 */
extern Allocator* g_allocatorHeap;

/**
 * Page allocator, allocates memory pages directly from the OS.
 * Note: All allocations will be rounded up to a multiple of the system page size.
 */
extern Allocator* g_allocatorPage;

/**
 * Initialize the global allocators.
 * Should be called once at application startup.
 */
void alloc_init();

/**
 * Create a new bump allocator. Will allocate from the given memory region, once the region is empty
 * allocations will fail. Memory region needs to contain atleast 32 bytes for internal book-keeping.
 */
Allocator* alloc_bump_create(Mem);

/**
 * Allocate new memory.
 * Note: Has to be explicitly freed using 'alloc_free'.
 */
Mem alloc_alloc(Allocator*, usize);

/**
 * Free previously allocated memory.
 * Pre-condition: Given memory was allocated from the same allocator.
 */
void alloc_free(Allocator*, Mem);

/**
 * Return the minimum allocation size (in bytes) for this allocator.
 * For example the page-allocator will return the page size.
 */
usize alloc_min_size(Allocator*);
