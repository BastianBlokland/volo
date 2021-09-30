#include "check_spec.h"
#include "core_alloc.h"
#include "core_array.h"
#include "core_memory.h"
#include "core_rng.h"

typedef struct {
  u32 a, b;
} TestMemStruct;

spec(memory) {

  it("can create a memory view over a stack allocated struct") {
    Mem mem = mem_empty;

    mem = mem_struct(TestMemStruct);
    check_require(mem_valid(mem));
    check(mem_as_t(mem, u32)[0] == 0);
    check(mem_as_t(mem, u32)[1] == 0);

    mem = mem_struct(TestMemStruct, .a = 42);
    check_require(mem_valid(mem));
    check(mem_as_t(mem, u32)[0] == 42);
    check(mem_as_t(mem, u32)[1] == 0);

    mem = mem_struct(TestMemStruct, .b = 42);
    check_require(mem_valid(mem));
    check(mem_as_t(mem, u32)[0] == 0);
    check(mem_as_t(mem, u32)[1] == 42);

    mem = mem_struct(TestMemStruct, .a = 1337, .b = 42);
    check_require(mem_valid(mem));
    check(mem_as_t(mem, u32)[0] == 1337);
    check(mem_as_t(mem, u32)[1] == 42);
  }

  it("can create a memory view from two pointers") {
    u8    rawMem[128] = {0};
    void* rawMemHead  = rawMem;
    void* rawMemTail  = bits_ptr_offset(rawMemHead, sizeof(rawMem));

    Mem mem = mem_from_to(rawMemHead, rawMemTail);
    check_eq_int(mem.size, 128);
    check(mem_begin(mem) == rawMemHead);
    check(mem_end(mem) == rawMemTail);
  }

  it("can check if it contains a specific byte") {
    Mem mem = array_mem(((u8[]){
        42,
        137,
        255,
        99,
    }));

    check(mem_contains(mem, 42));
    check(mem_contains(mem, 99));
    check(mem_contains(mem, 255));

    check(!mem_contains(mem, 7));
    check(!mem_contains(mem, 0));
  }

  it("can create a dynamicly sized allocation on the stack") {
    Rng* rng = rng_create_xorwow(g_alloc_scratch, 42);

    const usize size     = rng_sample_range(rng, 0, 2) ? 1234 : 1337;
    Mem         stackMem = mem_stack(size);

    mem_set(stackMem, 0xAF);
    for (usize i = 0; i != size; ++i) {
      check(*mem_at_u8(stackMem, i) == 0xAF);
    }
  }
}
