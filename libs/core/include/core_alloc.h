#pragma once
#include "core.h"
#include "core_memory.h"

/**
 * Create a bump allocator backed by a buffer on the stack. Allocations will fail once the buffer
 * has been filled up. Note: Allocations made from the allocator are not valid after the allocator
 * goes out of scope.
 * NOTE: Care must be taken not to overflow the stack by using too high _SIZE_ values.
 */
#define alloc_bump_create_stack(_SIZE_) alloc_bump_create(mem_stack(_SIZE_))

/**
 * Allocate new memory that satisfies the size and alignment required for the given type.
 * NOTE: Has to be explicitly freed using 'alloc_free'.
 */
#define alloc_alloc_t(_ALLOCATOR_, _TYPE_) alloc_array_t(_ALLOCATOR_, _TYPE_, 1)

#define alloc_array_t(_ALLOCATOR_, _TYPE_, _COUNT_)                                                \
  ((_TYPE_*)alloc_alloc((_ALLOCATOR_), sizeof(_TYPE_) * (_COUNT_), alignof(_TYPE_)).ptr)

/**
 * Free previously allocated memory.
 * Pre-condition: Given memory was allocated from the same allocator and with the same size.
 */
#define alloc_free_t(_ALLOCATOR_, _PTR_) alloc_free_array_t(_ALLOCATOR_, _PTR_, 1)

#define alloc_free_array_t(_ALLOCATOR_, _PTR_, _COUNT_)                                            \
  alloc_free((_ALLOCATOR_), mem_create((_PTR_), sizeof(*(_PTR_)) * (_COUNT_)))

/**
 * Allocator handle.
 */
typedef struct sAllocator Allocator;

/**
 * Routine to build an allocator to manage a memory region.
 */
typedef Allocator* (*AllocatorBuilder)(Mem);

/**
 * 'Normal' heap allocator.
 * NOTE: Thread-safe.
 */
extern Allocator* g_allocHeap;

/**
 * Page allocator, allocates memory pages directly from the OS.
 * NOTE: Thread-safe.
 */
extern Allocator* g_allocPage;

/**
 * Persistent allocator.
 * Allocator for memory that needs to persist over the whole application lifetime.
 * Memory cannot be manually freed, its automatically freed at application shutdown.
 * NOTE: Thread-safe.
 */
extern Allocator* g_allocPersist;

/**
 * Scratch allocator, allocates from a fixed size thread-local circular heap buffer.
 * Meant for very short lived allocations. As its backed by a fixed-size buffer allocations will be
 * overwritten once X new allocations have been made (where X is determined by the size of the
 * allocations and the size of the scratch buffer).
 */
extern THREAD_LOCAL Allocator* g_allocScratch;

/**
 * Create a new bump allocator. Will allocate from the given memory region, once the region is empty
 * allocations will fail. Memory region needs to contain atleast 64 bytes for internal book-keeping.
 * NOTE: Does not need explicit destruction as all book-keeping is stored within the given mem.
 */
Allocator* alloc_bump_create(Mem);

/**
 * Create a chunked allocator.
 * Allocates chunks of memory from the parent allocator and uses AllocatorBuilder to create
 * sub-allocators for those chunks.
 *
 * NOTE: Chunks are only freed when the allocator is destroyed.
 * NOTE: Destroy using 'alloc_chunked_destroy()'.
 * NOTE: Only 64 chunks are supported, after that allocations will fail.
 *
 * Pre-condition: chunkSize >= 768.
 * Pre-condition: chunkSize is a power-of-two.
 */
Allocator* alloc_chunked_create(Allocator* parent, AllocatorBuilder, usize chunkSize);
void       alloc_chunked_destroy(Allocator*);

/**
 * Create a fixed-size block allocator.
 * Allocates chunks of memory from the parent allocator and splits them into fixed size blocks.
 *
 * NOTE: Thread-safe.
 * NOTE: Chunks are only freed when the allocator is destroyed.
 * NOTE: Destroy using 'alloc_block_destroy()'
 *
 * Pre-condition: chunkSize >= 8
 * Pre-condition: blockAlign is a power-of-two.
 * Pre-condition: blockSize is a multiple of blockAlign.
 */
Allocator* alloc_block_create(Allocator* parent, usize blockSize, usize blockAlign);
void       alloc_block_destroy(Allocator*);

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
void alloc_maybe_free(Allocator*, Mem);

/**
 * Duplicate the given memory with memory alloced from the given allocator.
 * NOTE: Has to be explicitly freed using 'alloc_free'.
 */
Mem alloc_dup(Allocator*, Mem, usize align);
Mem alloc_maybe_dup(Allocator*, Mem, usize align);

/**
 * Return the maximum allocation size (in bytes) for this allocator.
 */
usize alloc_max_size(Allocator*);

/**
 * Reset the given allocator.
 * NOTE: Will invalidate all memory allocated from this allocator.
 */
void alloc_reset(Allocator*);

/**
 * Allocation statistics.
 * NOTE: Does not include global memory, stacks and memory allocated by external apis.
 */
typedef struct {
  u32   pageCount;
  usize pageTotal;      // Total number of bytes allocated by the page allocator.
  u64   pageCounter;    // Incremented on every page allocation.
  u64   heapActive;     // Total number of active allocations in the heap allocator.
  u64   heapCounter;    // Incremented on every heap allocation.
  u64   persistCounter; // Incremented on every persistent allocation.
} AllocStats;

AllocStats alloc_stats_query(void);

/**
 * Dump the active heap allocations to std-out.
 * NOTE: Requires memory-tracking to be compiled in.
 */
void alloc_heap_dump(void);
void alloc_persist_dump(void);
