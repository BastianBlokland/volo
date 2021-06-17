#include "core_alloc.h"
#include "core_diag.h"

static void test_alloc_scratch_alignment() {
  alloc_alloc(g_alloc_scratch, 32, 32); // Start with an alignment of (atleast) 32 bytes.

  u8* startAddr = mem_begin(alloc_alloc(g_alloc_scratch, 1, 1));
  u8* addr      = mem_begin(alloc_alloc(g_alloc_scratch, 6, 2));

  diag_assert(addr == startAddr + 2);

  addr = mem_begin(alloc_alloc(g_alloc_scratch, 8, 8));

  diag_assert(addr == startAddr + 8);

  addr = mem_begin(alloc_alloc(g_alloc_scratch, 64, 32));

  diag_assert(addr == startAddr + 32);
}

void test_alloc_scratch() { test_alloc_scratch_alignment(); }
