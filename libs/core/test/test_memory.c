#include "check_spec.h"
#include "core_alloc.h"
#include "core_array.h"
#include "core_memory.h"
#include "core_rng.h"

typedef struct {
  u32 a, b;
} TestMemStructA;

typedef struct {
  String a;
} TestMemStructB;

typedef struct {
  ALIGNAS(128) u32 val;
} TestMemAlignedStruct;

spec(memory) {

  it("can create a memory view over a stack allocated struct") {
    Mem mem = mem_empty;

    mem = mem_struct(TestMemStructA);
    check_require(mem_valid(mem));
    check(mem_as_t(mem, u32)[0] == 0);
    check(mem_as_t(mem, u32)[1] == 0);

    mem = mem_struct(TestMemStructA, .a = 42);
    check_require(mem_valid(mem));
    check(mem_as_t(mem, u32)[0] == 42);
    check(mem_as_t(mem, u32)[1] == 0);

    mem = mem_struct(TestMemStructA, .b = 42);
    check_require(mem_valid(mem));
    check(mem_as_t(mem, u32)[0] == 0);
    check(mem_as_t(mem, u32)[1] == 42);

    mem = mem_struct(TestMemStructA, .a = 1337, .b = 42);
    check_require(mem_valid(mem));
    check(mem_as_t(mem, u32)[0] == 1337);
    check(mem_as_t(mem, u32)[1] == 42);

    mem = mem_struct(TestMemStructB);
    check_require(mem_valid(mem));
    check(string_is_empty(*mem_as_t(mem, String)));

    mem = mem_struct(TestMemStructB, .a = string_lit("Hello World"));
    check_require(mem_valid(mem));
    check_eq_string(*mem_as_t(mem, String), string_lit("Hello World"));
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

  it("can read a little-endian encoded 8bit unsigned integer") {
    const u8 val = 42;
    Mem      mem = array_mem(((u8[]){val}));
    u8       out;
    check(mem_consume_le_u8(mem, &out).size == 0);
    check_eq_int(out, val);
  }

  it("can read a little-endian encoded 16bit unsigned integer") {
    const u16 val = 1337;
    Mem       mem = array_mem(((u8[]){
        (u8)val,
        (u8)(val >> 8),
    }));
    u16       out;
    check(mem_consume_le_u16(mem, &out).size == 0);
    check_eq_int(out, val);
  }

  it("can read a little-endian encoded 32bit unsigned integer") {
    const u32 val = 1337133742;
    Mem       mem = array_mem(((u8[]){
        (u8)val,
        (u8)(val >> 8),
        (u8)(val >> 16),
        (u8)(val >> 24),
    }));
    u32       out;
    check(mem_consume_le_u32(mem, &out).size == 0);
    check_eq_int(out, val);
  }

  it("can read a little-endian encoded 64bit unsigned integer") {
    const u64 val = u64_lit(12345678987654321234);
    Mem       mem = array_mem(((u8[]){
        (u8)val,
        (u8)(val >> 8),
        (u8)(val >> 16),
        (u8)(val >> 24),
        (u8)(val >> 32),
        (u8)(val >> 40),
        (u8)(val >> 48),
        (u8)(val >> 56),
    }));
    u64       out;
    check(mem_consume_le_u64(mem, &out).size == 0);
    check_eq_int(out, val);
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

    rng_destroy(rng);
  }

  it("can swap the contents of two memory locations") {
    Mem memA = mem_stack(64);
    Mem memB = mem_stack(64);

    mem_set(memA, 0xAA);
    mem_set(memB, 0xAB);

    check_require(mem_contains(memA, 0xAA));
    check_require(mem_contains(memB, 0xAB));
    check_require(!mem_contains(memA, 0xAB));
    check_require(!mem_contains(memB, 0xAA));

    mem_swap(memA, memB);

    check_require(!mem_contains(memA, 0xAA));
    check_require(!mem_contains(memB, 0xAB));
    check_require(mem_contains(memA, 0xAB));
    check_require(mem_contains(memB, 0xAA));
  }

  it("respects struct alignment") {
    TestMemAlignedStruct data = {0};
    check(bits_aligned_ptr(&data.val, 128));

    Mem m = mem_struct(TestMemAlignedStruct, .val = 42);
    check(bits_aligned_ptr(m.ptr, 128));
  }
}
