#include "core_bits.h"
#include "core_math.h"

#include "alloc_internal.h"

typedef struct {
  Allocator api;
  usize     pageSize;
} AllocatorPageCache;

static Mem alloc_pagecache_alloc(Allocator* allocator, const usize size, const usize align) {
  (void)allocator;
  return alloc_alloc(g_allocPage, size, align);
}

static void alloc_pagecache_free(Allocator* allocator, const Mem mem) {
  (void)allocator;
  return alloc_free(g_allocPage, mem);
}

static usize alloc_pagecache_max_size(Allocator* allocator) {
  (void)allocator;
  return alloc_max_alloc_size;
}

static void alloc_pagecache_reset(Allocator* allocator) { (void)allocator; }

static AllocatorPageCache g_allocatorIntern;

Allocator* alloc_pagecache_init(void) {
  g_allocatorIntern = (AllocatorPageCache){
      .api =
          {
              .alloc   = alloc_pagecache_alloc,
              .free    = alloc_pagecache_free,
              .maxSize = alloc_pagecache_max_size,
              .reset   = alloc_pagecache_reset,
          },
      .pageSize = alloc_page_size(),
  };
  if (UNLIKELY(!g_allocatorIntern.pageSize)) {
    alloc_crash_with_msg("Invalid page-size");
  }
  return (Allocator*)&g_allocatorIntern;
}
