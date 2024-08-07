#include "check_spec.h"
#include "core_alloc.h"

spec(alloc_scratch) {
  it("respects the requested alignment") {
    alloc_alloc(g_allocScratch, 32, 32); // Start with an alignment of (atleast) 32 bytes.

    u8* startAddr = mem_begin(alloc_alloc(g_allocScratch, 1, 1));
    u8* addr      = mem_begin(alloc_alloc(g_allocScratch, 6, 2));

    check(addr == startAddr + 2);

    addr = mem_begin(alloc_alloc(g_allocScratch, 8, 8));

    check(addr == startAddr + 8);

    addr = mem_begin(alloc_alloc(g_allocScratch, 64, 32));

    check(addr == startAddr + 32);
  }
}
