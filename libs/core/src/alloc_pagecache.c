#include "core_array.h"
#include "core_bits.h"
#include "core_math.h"
#include "core_thread.h"

#include "alloc_internal.h"

/**
 * Wrapper around the page allocator that caches allocations that are only a few pages, this avoids
 * allot of sys-call traffic when relatively small allocations are freed and reallocated.
 */

#define pagecache_pages_max 4

typedef struct sPageCacheNode {
  struct sPageCacheNode* next;
} PageCacheNode;

typedef struct {
  Allocator      api;
  ThreadSpinLock spinLock;
  usize          pageSize;
  PageCacheNode* freeNodes[pagecache_pages_max];
} AllocatorPageCache;

static u32 pagecache_num_pages(AllocatorPageCache* cache, const usize size) {
  return (u32)((size + cache->pageSize - 1) / cache->pageSize);
}

static Mem pagecache_alloc(Allocator* allocator, const usize size, const usize align) {
  AllocatorPageCache* cache = (AllocatorPageCache*)allocator;

#ifndef VOLO_FAST
  if (UNLIKELY(!bits_aligned(cache->pageSize, align))) {
    alloc_crash_with_msg(
        "pagecache_alloc: Alignment '{}' invalid (stronger then pageSize)", fmt_int(align));
  }
#endif

  const u32 numPages = pagecache_num_pages(cache, size);
  if (numPages > pagecache_pages_max) {
    goto NewAllocation;
  }

  Mem result = mem_empty;
  thread_spinlock_lock(&cache->spinLock);
  {
    PageCacheNode* cacheNode = cache->freeNodes[numPages - 1];
    if (cacheNode) {
      alloc_unpoison(mem_create(cacheNode, numPages * cache->pageSize));
      cache->freeNodes[numPages - 1] = cacheNode->next;

      result = mem_create(cacheNode, size);
    }
  }
  thread_spinlock_unlock(&cache->spinLock);

  if (mem_valid(result)) {
    return result;
  }

NewAllocation:;
  const Mem newAlloc = alloc_alloc(g_allocPage, numPages * cache->pageSize, cache->pageSize);
  if (mem_valid(newAlloc)) {
    return mem_slice(newAlloc, 0, size); // Return the size in the requested size.
  }
  return mem_empty;
}

static void pagecache_free(Allocator* allocator, const Mem mem) {
#ifndef VOLO_FAST
  if (UNLIKELY(!mem_valid(mem))) {
    alloc_crash_with_msg("pagecache_free: Invalid allocation");
  }
#endif
  AllocatorPageCache* cache    = (AllocatorPageCache*)allocator;
  const u32           numPages = pagecache_num_pages(cache, mem.size);
  if (numPages > pagecache_pages_max) {
    goto FreeAllocation;
  }

  alloc_tag_free(mem, AllocMemType_Normal);

  thread_spinlock_lock(&cache->spinLock);
  {
    PageCacheNode* cacheNode       = mem.ptr;
    *cacheNode                     = (PageCacheNode){.next = cache->freeNodes[numPages - 1]};
    cache->freeNodes[numPages - 1] = cacheNode;

    alloc_poison(mem_create(cacheNode, numPages * cache->pageSize));
  }
  thread_spinlock_unlock(&cache->spinLock);
  return;

FreeAllocation:
  return alloc_free(g_allocPage, mem_create(mem.ptr, numPages * cache->pageSize));
}

static usize pagecache_max_size(Allocator* allocator) {
  (void)allocator;
  return alloc_max_alloc_size;
}

static void pagecache_reset(Allocator* allocator) {
  AllocatorPageCache* cache = (AllocatorPageCache*)allocator;
  thread_spinlock_lock(&cache->spinLock);
  {
    for (u32 i = 0; i != array_elems(cache->freeNodes); ++i) {
      for (PageCacheNode* cacheNode = cache->freeNodes[i]; cacheNode;) {
        const Mem nodeMem = mem_create(cacheNode, (i + 1) * cache->pageSize);
        cacheNode         = cacheNode->next;
        alloc_free(g_allocPage, nodeMem);
      }
      cache->freeNodes[i] = null;
    }
  }
  thread_spinlock_unlock(&cache->spinLock);
}

static AllocatorPageCache g_allocatorIntern;

Allocator* alloc_pagecache_init(void) {
  g_allocatorIntern = (AllocatorPageCache){
      .api =
          {
              .alloc   = pagecache_alloc,
              .free    = pagecache_free,
              .maxSize = pagecache_max_size,
              .reset   = pagecache_reset,
          },
      .pageSize = alloc_page_size(),
  };
  if (UNLIKELY(!g_allocatorIntern.pageSize)) {
    alloc_crash_with_msg("Invalid page-size");
  }
  return (Allocator*)&g_allocatorIntern;
}

void alloc_pagecache_teardown(void) {
  pagecache_reset(&g_allocatorIntern.api);
  g_allocatorIntern = (AllocatorPageCache){0};
}
