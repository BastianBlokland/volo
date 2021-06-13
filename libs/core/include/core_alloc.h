#pragma once
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
 * Allocator handle.
 */
typedef struct sAllocator Allocator;

/**
 * 'Normal' heap allocator, semantics similar to 'malloc'.
 */
extern Allocator* g_alloc_heap;

/**
 * Page allocator, allocates memory pages directly from the OS.
 * Note: All allocations will be rounded up to a multiple of the system page size.
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

/**
 * Return the maximum allocation size (in bytes) for this allocator.
 */
usize alloc_max_size(Allocator*);
