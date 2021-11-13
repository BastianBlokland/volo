#include "core_alloc.h"
#include "core_diag.h"

#include "alloc_internal.h"

#define alloc_persist_chunk_size (usize_mebibyte * 1)

static Allocator* g_persist;

Allocator* alloc_persist_init() {
  diag_assert(!g_persist);
  g_persist = alloc_chunked_create(g_alloc_page, alloc_bump_create, alloc_persist_chunk_size);
  return g_persist;
}

void alloc_persist_teardown() {
  diag_assert(g_persist);
  alloc_chunked_destroy(g_persist);
  g_persist = null;
}
