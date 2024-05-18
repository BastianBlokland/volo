#include "core_alloc.h"
#include "core_diag.h"
#include "core_file.h"
#include "core_thread.h"

#include "alloc_internal.h"

/**
 * Allocator for allocations that will persist for the entire application lifetime.
 * Memory cannot be manually freed, its automatically freed at application shutdown.
 *
 * Implemented with a set of fixed-size chunks with simple bump allocators on the chunks.
 */

#define alloc_persist_chunk_size (usize_mebibyte * 1)

typedef struct {
  Allocator      api;
  Allocator*     chunkedAlloc;
  ThreadSpinLock spinLock;

#ifdef VOLO_MEMORY_TRACKING
  AllocTracker* tracker;
#endif
  i64 counter; // Incremented on every allocation.
} AllocatorPersist;

static void alloc_persist_lock(AllocatorPersist* allocPersist) {
  thread_spinlock_lock(&allocPersist->spinLock);
}

static void alloc_persist_unlock(AllocatorPersist* allocPersist) {
  thread_spinlock_unlock(&allocPersist->spinLock);
}

static Mem alloc_persist_alloc(Allocator* allocator, const usize size, const usize align) {
  AllocatorPersist* allocPersist = (AllocatorPersist*)allocator;

  alloc_persist_lock(allocPersist);
  ++allocPersist->counter;

  const Mem result = alloc_alloc(allocPersist->chunkedAlloc, size, align);
#ifdef VOLO_MEMORY_TRACKING
  if (LIKELY(mem_valid(result))) {
    alloc_tracker_add(allocPersist->tracker, result, symbol_stack_walk());
  }
#endif
  alloc_persist_unlock(allocPersist);
  return result;
}

static usize alloc_persist_max_size(Allocator* allocator) {
  AllocatorPersist* allocPersist = (AllocatorPersist*)allocator;

  alloc_persist_lock(allocPersist);
  const usize result = alloc_max_size(allocPersist->chunkedAlloc);
  alloc_persist_unlock(allocPersist);
  return result;
}

static AllocatorPersist g_allocatorIntern;

Allocator* alloc_persist_init(void) {
  g_allocatorIntern = (AllocatorPersist){
      (Allocator){
          .alloc   = alloc_persist_alloc,
          .free    = null,
          .maxSize = alloc_persist_max_size,
          .reset   = null,
      },
#ifdef VOLO_MEMORY_TRACKING
      .tracker = alloc_tracker_create(),
#endif
      .chunkedAlloc =
          alloc_chunked_create(g_allocPage, alloc_bump_create, alloc_persist_chunk_size),
  };
  return (Allocator*)&g_allocatorIntern;
}

void alloc_persist_teardown(void) {
#ifdef VOLO_MEMORY_TRACKING
  alloc_tracker_destroy(g_allocatorIntern.tracker);
#endif
  alloc_chunked_destroy(g_allocatorIntern.chunkedAlloc);
  g_allocatorIntern = (AllocatorPersist){0};
}

u64 alloc_persist_counter(void) {
  alloc_persist_lock(&g_allocatorIntern);
  const u64 result = (u64)g_allocatorIntern.counter;
  alloc_persist_unlock(&g_allocatorIntern);
  return result;
}

void alloc_persist_dump(void) {
#ifdef VOLO_MEMORY_TRACKING
  alloc_tracker_dump_file(g_allocatorIntern.tracker, g_fileStdOut);
#endif
}
