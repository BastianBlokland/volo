#include "check/spec.h"
#include "core/alloc.h"
#include "core/diag.h"

spec(alloc_block) {

  static const usize g_memSize   = 32 * usize_kibibyte;
  static const usize g_blockSize = 32;

  Mem        memTotal       = mem_empty;
  usize      memSizeUseable = 0;
  Allocator* allocParent    = null;
  Allocator* allocBlock     = null;

  setup() {
    memTotal       = alloc_alloc(g_allocHeap, g_memSize, 1);
    allocParent    = alloc_bump_create(memTotal);
    memSizeUseable = alloc_max_size(allocParent);
    allocBlock     = alloc_block_create(allocParent, g_blockSize, g_blockSize);
  }

  it("store blocks sequentially in memory") {
    Mem lastMem;
    for (usize i = 0; i != 100; ++i) {
      Mem mem = alloc_alloc(allocBlock, g_blockSize, 1);
      check_require(mem_valid(mem));
      if (i) {
        check(mem_begin(lastMem) == mem_end(mem)); // The free-list is initialized in reverse order.
      }
      lastMem = mem;
    }
  }

  it("allocates new chunks when space runs out") {
    const usize startingSize = alloc_max_size(allocParent);
    check(startingSize < memSizeUseable); // it will make an initial allocation.

    for (usize i = 0; i != 256; ++i) {
      check(mem_valid(alloc_alloc(allocBlock, g_blockSize, 1)));
    }
    check(alloc_max_size(allocParent) < memSizeUseable);
  }

  it("reuses freed blocks immediately") {
    const Mem memA = alloc_alloc(allocBlock, g_blockSize, 1);
    const Mem memB = alloc_alloc(allocBlock, g_blockSize, 1);

    check(memA.ptr != memB.ptr);

    alloc_free(allocBlock, memA);
    alloc_free(allocBlock, memB);

    const Mem memC = alloc_alloc(allocBlock, g_blockSize, 1);
    const Mem memD = alloc_alloc(allocBlock, g_blockSize, 1);

    check(memC.ptr == memB.ptr);
    check(memD.ptr == memA.ptr);
  }

  it("fails allocations bigger then the block-size") {
    const Mem mem = alloc_alloc(allocBlock, 37, 1);
    check(!mem_valid(mem));
  }

  it("can be reset") {
    u32 numAllocs = 0;
    Mem mem;
    do {
      mem = alloc_alloc(allocBlock, g_blockSize, 1);
      numAllocs++;
    } while (mem_valid(mem));

    alloc_reset(allocBlock);

    for (u32 i = 1; i != numAllocs; ++i) {
      check(mem_valid(alloc_alloc(allocBlock, g_blockSize, 1)));
    }
  }

  it("returns the block-size as the max size") {
    check_eq_int(alloc_max_size(allocBlock), g_blockSize);
  }

  teardown() {
    alloc_reset(allocBlock); // Suppress leak-detection complaining.
    alloc_block_destroy(allocBlock);

    // Verify that all memory was returned to the parent.
    diag_assert(alloc_max_size(allocParent) == memSizeUseable);

    alloc_free(g_allocHeap, memTotal);
  }
}
