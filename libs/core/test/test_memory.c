#include "core_array.h"
#include "core_diag.h"
#include "core_memory.h"

static void test_memory_contains() {
  Mem mem = array_mem(((u8[]){
      42,
      137,
      255,
      99,
  }));

  diag_assert(mem_contains(mem, 42));
  diag_assert(mem_contains(mem, 99));
  diag_assert(mem_contains(mem, 255));

  diag_assert(!mem_contains(mem, 7));
  diag_assert(!mem_contains(mem, 0));
}

void test_memory() { test_memory_contains(); }
