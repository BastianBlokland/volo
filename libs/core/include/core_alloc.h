#pragma once
#include "core_alignof.h"
#include "core_annotation.h"
#include "core_memory.h"

/**
 * Create a bump allocator backed by a buffer on the stack. Allocations will fail once the buffer
 * has been filled up. Note: Allocations made from the allocator are not valid after the allocator
 * goes out of scope. Note: Care must be taken not to overflow the stack by using too high _SIZE_
 * values.
 */
#define alloc_bump_create_stack(_SIZE_) alloc_bump_create(mem_stack(_SIZE_))

/**
 * Allocate new memory that satisfies the size and alignment required for the given type.
 * NOTE: Has to be explicitly freed using 'alloc_free'.
 */
#define alloc_alloc_t(_ALLOCATOR_, _TYPE_)                                                         \
  ((_TYPE_*)alloc_alloc((_ALLOCATOR_), sizeof(_TYPE_), alignof(_TYPE_)).ptr)

/**
 * Free previously allocated memory.
 * Pre-condition: Given memory was allocated from the same allocator and with the same size.
 */
#define alloc_free_t(_ALLOCATOR_, _PTR_)                                                           \
  alloc_free((_ALLOCATOR_), mem_create((_PTR_), sizeof(*(_PTR_))))

/**
 * Allocator handle.
 */
typedef struct sAllocator Allocator;

/**
 * 'Normal' heap allocator.
 */
extern Allocator* g_alloc_heap;

/**
 * Page allocator, allocates memory pages directly from the OS.
 */
extern Allocator* g_alloc_page;

/**
 * Scratch allocator, allocates from a fixed size thread-local circular heap buffer.
 * Meant for very short lived allocations. As its backed by a fixed-size buffer allocations will be
 * overwritten once X new allocations have been made (where X is determined by the size of the
 * allocations and the size of the scratch buffer).
 */
extern THREAD_LOCAL Allocator* g_alloc_scratch;

/**
 * Create a new bump allocator. Will allocate from the given memory region, once the region is empty
 * allocations will fail. Memory region needs to contain atleast 64 bytes for internal book-keeping.
 */
Allocator* alloc_bump_create(Mem);

/**
 * Allocate new memory.
 * NOTE: Has to be explicitly freed using 'alloc_free'.
 * Pre-condition: size > 0.
 * Pre-condition: align is a power-of-two.
 * Pre-condition: size is a multiple of align.
 */
Mem alloc_alloc(Allocator*, usize size, usize align);

/**
 * Free previously allocated memory.
 * Pre-condition: Given memory was allocated from the same allocator.
 */
void alloc_free(Allocator*, Mem);

/**
 * Duplicate the given memory with memory alloced from the given allocator.
 * NOTE: Has to be explicitly freed using 'alloc_free'.
 */
Mem alloc_dup(Allocator*, Mem, usize align);

/**
 * Return the minimum allocation size (in bytes) for this allocator.
 * For example the page-allocator will return the page size.
 */
usize alloc_min_size(Allocator*);

/**
 * Return the maximum allocation size (in bytes) for this allocator.
 */
usize alloc_max_size(Allocator*);
