#include "check/spec.h"
#include "core/alloc.h"
#include "core/array.h"
#include "core/bits.h"
#include "core/memory.h"
#include "core/rng.h"

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

  it("can create a memory view over a variable") {
    const i64 val1 = 42;
    const Mem mem1 = mem_var(val1);

    check(mem1.ptr == &val1);
    check_eq_int(mem1.size, sizeof(i64));
    check_eq_int(*mem_as_t(mem1, i64), 42);

    const i32 val2[8] = {42};
    const Mem mem2    = mem_var(val2);

    check(mem2.ptr == val2);
    check_eq_int(mem2.size, sizeof(i32) * 8);
    check_eq_int(*mem_as_t(mem2, i32), 42);
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

  it("can splat a value over a memory range") {
    const usize valueCount = 4;
    const u32   value      = 133337;
    const Mem   target     = mem_stack(sizeof(value) * valueCount);

    mem_splat(target, mem_var(value));

    const u32* targetValues = mem_as_t(target, u32);
    for (usize i = 0; i != valueCount; ++i) {
      check_eq_int(targetValues[i], value);
    }
  }

  it("can read a 8bit unsigned integer") {
    const u8 val = 42;
    Mem      mem = array_mem(((u8[]){val}));
    u8       out;
    check(mem_consume_u8(mem, &out).size == 0);
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

  it("can read a big-endian encoded 16bit unsigned integer") {
    const u16 val = 1337;
    Mem       mem = array_mem(((u8[]){
        (u8)(val >> 8),
        (u8)val,
    }));
    u16       out;
    check(mem_consume_be_u16(mem, &out).size == 0);
    check_eq_int(out, val);
  }

  it("can read a big-endian encoded 32bit unsigned integer") {
    const u32 val = 1337133742;
    Mem       mem = array_mem(((u8[]){
        (u8)(val >> 24),
        (u8)(val >> 16),
        (u8)(val >> 8),
        (u8)val,
    }));
    u32       out;
    check(mem_consume_be_u32(mem, &out).size == 0);
    check_eq_int(out, val);
  }

  it("can read a big-endian encoded 64bit unsigned integer") {
    const u64 val = u64_lit(12345678987654321234);
    Mem       mem = array_mem(((u8[]){
        (u8)(val >> 56),
        (u8)(val >> 48),
        (u8)(val >> 40),
        (u8)(val >> 32),
        (u8)(val >> 24),
        (u8)(val >> 16),
        (u8)(val >> 8),
        (u8)val,
    }));
    u64       out;
    check(mem_consume_be_u64(mem, &out).size == 0);
    check_eq_int(out, val);
  }

  it("can write a 8bit unsigned integer") {
    const u8  val    = 42;
    const Mem buffer = mem_stack(1);

    check(mem_write_u8(buffer, val).size == 0);

    u8 out;
    mem_consume_u8(buffer, &out);
    check_eq_int(out, val);
  }

  it("can zero memory") {
    const Mem bufferA = mem_stack(42);
    mem_write_u8_zero(bufferA, 42);

    const Mem bufferB = mem_stack(42);
    mem_set(bufferB, 0);

    check(mem_eq(bufferA, bufferB));
  }

  it("can write a little-endian encoded 16bit unsigned integer") {
    const u16 val    = 1337;
    const Mem buffer = mem_stack(2);

    check(mem_write_le_u16(buffer, val).size == 0);

    u16 out;
    mem_consume_le_u16(buffer, &out);
    check_eq_int(out, val);
  }

  it("can write a little-endian encoded 32bit unsigned integer") {
    const u32 val    = 1337133742;
    const Mem buffer = mem_stack(4);

    check(mem_write_le_u32(buffer, val).size == 0);

    u32 out;
    mem_consume_le_u32(buffer, &out);
    check_eq_int(out, val);
  }

  it("can write a little-endian encoded 64bit unsigned integer") {
    const u64 val    = u64_lit(12345678987654321234);
    const Mem buffer = mem_stack(8);

    check(mem_write_le_u64(buffer, val).size == 0);

    u64 out;
    mem_consume_le_u64(buffer, &out);
    check_eq_int(out, val);
  }

  it("can write a big-endian encoded 16bit unsigned integer") {
    const u16 val    = 1337;
    const Mem buffer = mem_stack(2);

    check(mem_write_be_u16(buffer, val).size == 0);

    u16 out;
    mem_consume_be_u16(buffer, &out);
    check_eq_int(out, val);
  }

  it("can write a big-endian encoded 32bit unsigned integer") {
    const u32 val    = 1337133742;
    const Mem buffer = mem_stack(4);

    check(mem_write_be_u32(buffer, val).size == 0);

    u32 out;
    mem_consume_be_u32(buffer, &out);
    check_eq_int(out, val);
  }

  it("can write a big-endian encoded 64bit unsigned integer") {
    const u64 val    = u64_lit(12345678987654321234);
    const Mem buffer = mem_stack(8);

    check(mem_write_be_u64(buffer, val).size == 0);

    u64 out;
    mem_consume_be_u64(buffer, &out);
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

  it("can check if all bytes are equal to specific byte") {
    Mem memA = array_mem(((u8[]){
        1,
        1,
        1,
    }));

    check(!mem_all(memA, 0));
    check(mem_all(memA, 1));

    Mem memB = array_mem(((u8[]){
        1,
        2,
        3,
    }));
    check(!mem_all(memB, 1));
    check(!mem_all(memB, 2));
    check(!mem_all(memB, 3));
  }

  it("can create a dynamicly sized allocation on the stack") {
    Rng* rng = rng_create_xorwow(g_allocScratch, 42);

    const usize size     = (usize)rng_sample_range(rng, 0, 2) ? 1234 : 1337;
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
