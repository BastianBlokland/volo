#include "check_spec.h"
#include "core_alloc.h"
#include "core_array.h"

spec(alloc_chunked) {

  it("allocates sequential allocations from the same chunk") {
    Allocator* alloc = alloc_chunked_create(g_alloc_heap, alloc_bump_create, 512);

    Mem lastMem;
    for (usize i = 0; i != 10; ++i) {
      Mem mem = alloc_alloc(alloc, 10, 1);
      check_require(mem_valid(mem));
      if (i) {
        check(mem_end(lastMem) == mem_begin(mem));
      }
      lastMem = mem;
    }

    alloc_chunked_destroy(alloc);
  }

  it("can free allocated memory") {
    Allocator* alloc = alloc_chunked_create(g_alloc_heap, alloc_bump_create, 512);

    // Small allocation to create the first chunk.
    alloc_alloc(alloc, 10, 1);

    // Because its backed by a bump-allocator we can trivially get the remaining size.
    usize remainingSizeInChunk = alloc_max_size(alloc);

    Mem mem = alloc_alloc(alloc, 100, 1); // Allocate 100 bytes.
    remainingSizeInChunk -= 100;

    // Verify that the expected amount was allocated from the chunk's bump-allocator.
    check_eq_int(alloc_max_size(alloc), remainingSizeInChunk);

    alloc_free(alloc, mem);
    remainingSizeInChunk += 100;

    // Verify that the expected amount was returned to the chunk's bump-allocator.
    check_eq_int(alloc_max_size(alloc), remainingSizeInChunk);

    alloc_chunked_destroy(alloc);
  }

  it("can create up to 64 chunks") {
    Allocator* alloc = alloc_chunked_create(g_alloc_heap, alloc_bump_create, 512);

    static const usize g_allocSize = 300;
    Mem                data[64];
    for (usize i = 0; i != array_elems(data); ++i) {
      data[i] = alloc_alloc(alloc, g_allocSize, 1);
      check_require(mem_valid(data[i]));
      mem_set(data[i], (u8)i);
    }

    // The 65'th allocation should fail.
    Mem mem = alloc_alloc(alloc, g_allocSize, 1);
    check(!mem_valid(mem));

    // Verify that all chunks contains the expected memory.
    Mem expected = mem_stack(g_allocSize);
    for (usize i = 0; i != array_elems(data); ++i) {
      mem_set(expected, (u8)i);
      check(mem_eq(data[i], expected));
    }

    alloc_chunked_destroy(alloc);
  }

  it("fails allocations bigger then the chunk-size") {
    Allocator* alloc = alloc_chunked_create(g_alloc_heap, alloc_bump_create, 512);

    Mem mem = alloc_alloc(alloc, 1024, 1);
    check(!mem_valid(mem));

    alloc_chunked_destroy(alloc);
  }

  it("can be reset") {
    Allocator* alloc = alloc_chunked_create(g_alloc_heap, alloc_bump_create, 512);

    // Fill 64 chunks with data.
    for (usize i = 0; i != 64; ++i) {
      Mem mem = alloc_alloc(alloc, 300, 1);
      check(mem_valid(mem));
    }

    // Verify that further allocations fail.
    check(!mem_valid(alloc_alloc(alloc, 300, 1)));

    // Reset the allocator.
    alloc_reset(alloc);

    // Verify that 64 chunks can be filled again with data.
    for (usize i = 0; i != 64; ++i) {
      Mem mem = alloc_alloc(alloc, 300, 1);
      check(mem_valid(mem));
    }

    alloc_chunked_destroy(alloc);
  }

  it("can return the maximum allocatable size in any chunk") {
    Allocator* alloc = alloc_chunked_create(g_alloc_heap, alloc_bump_create, 512);

    // Zero because no chunk has been created yet.
    // NOTE: Does this semantic actually make sense?
    check_eq_int(alloc_max_size(alloc), 0);

    // After an allocation the first chunk has been created and 'alloc_max_size' for the
    // bump-allocator returns the remaining size.
    alloc_alloc(alloc, 10, 1);
    check(alloc_max_size(alloc) > 250);

    alloc_chunked_destroy(alloc);
  }

  it("can use os memory pages as chunks") {
    Allocator* alloc = alloc_chunked_create(g_alloc_page, alloc_bump_create, 4096);

    // NOTE: '- 64' as the bump-allocator needs space for its internal book-keeping.
    Mem page = alloc_alloc(alloc, 4096 - 64, 64);
    check(mem_valid(page));

    alloc_chunked_destroy(alloc);
  }
}
