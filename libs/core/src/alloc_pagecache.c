#include "core_array.h"
#include "core_bits.h"
#include "core_thread.h"

#include "alloc_internal.h"

/**
 * Wrapper around the page allocator that caches allocations that are only a few pages, this avoids
 * allot of sys-call traffic when relatively small allocations are freed and reallocated.
 */

#define pagecache_pages_max 8

static const u32 g_pageCacheCountMax[pagecache_pages_max] = {
    [0] /* 1 * pageSize (4 KiB) */  = 1024,
    [1] /* 2 * pageSize (8 KiB) */  = 1024,
    [2] /* 3 * pageSize (12 KiB) */ = 1024,
    [3] /* 4 * pageSize (16 KiB) */ = 1024,
    [4] /* 5 * pageSize (20 KiB) */ = 512,
    [5] /* 6 * pageSize (24 KiB) */ = 512,
    [6] /* 7 * pageSize (28 KiB) */ = 512,
    [7] /* 8 * pageSize (32 KiB) */ = 512,
};

static const u32 g_pageCacheCountInitial[pagecache_pages_max] = {
    [0] /* 1 * pageSize (4 KiB) */  = 512,
    [1] /* 2 * pageSize (8 KiB) */  = 512,
    [2] /* 3 * pageSize (12 KiB) */ = 64,
    [3] /* 4 * pageSize (16 KiB) */ = 768,
    [4] /* 5 * pageSize (20 KiB) */ = 24,
    [5] /* 6 * pageSize (24 KiB) */ = 24,
    [6] /* 7 * pageSize (28 KiB) */ = 24,
    [7] /* 8 * pageSize (32 KiB) */ = 64,
};

typedef struct sPageCacheNode {
  struct sPageCacheNode* next;
} PageCacheNode;

typedef struct {
  Allocator      api;
  ThreadSpinLock spinLock;
  usize          pageSize;
  Mem            warmupMem;
  PageCacheNode* freeNodes[pagecache_pages_max];
  u32            freeNodesCount[pagecache_pages_max];
} AllocatorPageCache;

static bool pagecache_is_warmup(AllocatorPageCache* cache, const Mem mem) {
  if (!mem_valid(cache->warmupMem)) {
    return false;
  }
  const void* warmupBegin = mem_begin(cache->warmupMem);
  const void* warmupEnd   = mem_end(cache->warmupMem);
  return mem.ptr >= warmupBegin && mem.ptr < warmupEnd;
}

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
#else
  (void)align;
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
      cache->freeNodesCount[numPages - 1]--;

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
  const bool isWarmup = pagecache_is_warmup(cache, mem);
  if (!isWarmup && cache->freeNodesCount[numPages - 1] >= g_pageCacheCountMax[numPages - 1]) {
    goto FreeAllocation; // Already have enough cached of this size.
  }

  alloc_tag_free(mem, AllocMemType_Normal);

  thread_spinlock_lock(&cache->spinLock);
  {
    PageCacheNode* cacheNode = mem.ptr;
    *cacheNode               = (PageCacheNode){.next = cache->freeNodes[numPages - 1]};

    cache->freeNodes[numPages - 1] = cacheNode;
    cache->freeNodesCount[numPages - 1]++;

    alloc_poison(mem_create(cacheNode, numPages * cache->pageSize));
  }
  thread_spinlock_unlock(&cache->spinLock);
  return;

FreeAllocation:
  alloc_free(g_allocPage, mem_create(mem.ptr, numPages * cache->pageSize));
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
        alloc_unpoison(nodeMem);

        cacheNode = cacheNode->next;
        if (!pagecache_is_warmup(cache, nodeMem)) {
          alloc_free(g_allocPage, nodeMem);
        }
      }
      cache->freeNodes[i]      = null;
      cache->freeNodesCount[i] = 0;
    }

    if (mem_valid(cache->warmupMem)) {
      alloc_free(g_allocPage, cache->warmupMem);
      cache->warmupMem = mem_empty;
    }
  }
  thread_spinlock_unlock(&cache->spinLock);
}

static usize pagecache_warmup_size(AllocatorPageCache* cache) {
  usize result = 0;
  for (u32 sizeIdx = 0; sizeIdx != array_elems(cache->freeNodes); ++sizeIdx) {
    const usize numPages = sizeIdx + 1;
    result += numPages * cache->pageSize * g_pageCacheCountInitial[sizeIdx];
  }
  return result;
}

static void pagecache_warmup(AllocatorPageCache* cache) {
  diag_assert(!mem_valid(cache->warmupMem));

  const usize warmupSize = pagecache_warmup_size(cache);
  cache->warmupMem       = alloc_alloc(g_allocPage, warmupSize, cache->pageSize);
  if (!mem_valid(cache->warmupMem)) {
    alloc_crash_with_msg("pagecache_warmup: Failed to allocate warmup memory");
  }

  void* warmupPtr = cache->warmupMem.ptr;

  for (u32 sizeIdx = 0; sizeIdx != array_elems(cache->freeNodes); ++sizeIdx) {
    const usize numPages = sizeIdx + 1;
    for (u32 i = 0; i != g_pageCacheCountInitial[sizeIdx]; ++i) {
      PageCacheNode* cacheNode = warmupPtr;
      *cacheNode               = (PageCacheNode){.next = cache->freeNodes[sizeIdx]};

      cache->freeNodes[sizeIdx] = cacheNode;
      cache->freeNodesCount[sizeIdx]++;

      warmupPtr = bits_ptr_offset(warmupPtr, numPages * cache->pageSize);
    }
  }

  diag_assert(warmupPtr == mem_end(cache->warmupMem));

  alloc_poison(cache->warmupMem);
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

  pagecache_warmup(&g_allocatorIntern);

  return (Allocator*)&g_allocatorIntern;
}

void alloc_pagecache_teardown(void) {
  pagecache_reset(&g_allocatorIntern.api);
  g_allocatorIntern = (AllocatorPageCache){0};
}
