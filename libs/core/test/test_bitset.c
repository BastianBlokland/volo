#include "check_spec.h"
#include "core_array.h"
#include "core_bitset.h"

spec(bitset) {

  it("can test for non-set bits") {
    const BitSet zero64 = bitset_from_var((u64){0});

    check_eq_int(bitset_size(zero64), 64);
    check_eq_int(bitset_count(zero64), 0);
    for (u32 i = 0; i != 64; ++i) {
      check(!bitset_test(zero64, i));
    }
  }

  it("can test for set bits") {
    const BitSet ones64 = bitset_from_var((u64){~(u64)0});

    check_eq_int(bitset_size(ones64), 64);
    check_eq_int(bitset_count(ones64), 64);
    for (u32 i = 0; i != 64; ++i) {
      check(bitset_test(ones64, i));
    }
  }

  it("can find set bits") {
    u64    val[32] = {0};
    BitSet bits    = bitset_from_array(val);

    check_eq_int(bitset_size(bits), 64 * 32);

    // Check no bit is set.
    check_eq_int(bitset_count(bits), 0);
    for (u32 i = 0; i != 64; ++i) {
      check(!bitset_test(bits, i));
    }

    bitset_set(bits, 1337);
    check(bitset_test(bits, 1337));
    check_eq_int(bitset_count(bits), 1);

    bitset_set(bits, 42);
    check(bitset_test(bits, 42));
    check_eq_int(bitset_count(bits), 2);

    // Clear the set bits.
    bitset_clear(bits, 42);
    bitset_clear(bits, 1337);

    // Check no bit is set.
    check_eq_int(bitset_count(bits), 0);
    for (u32 i = 0; i != 64; ++i) {
      check(!bitset_test(bits, i));
    }
  }

  it("can count set bits") {
    BitSet bits = bitset_from_var((u64){0});

    bitset_set(bits, 0);
    bitset_set(bits, 63);
    bitset_set(bits, 42);
    bitset_set(bits, 13);
    bitset_set(bits, 51);

    check_eq_int(bitset_count(bits), 5);
  }

  it("can check if any bit is set") {
    BitSet bits = bitset_from_var((u64){0});
    for (u32 i = 0; i != 64; ++i) {
      check(!bitset_any(bits));
      bitset_set(bits, i);
      check(bitset_any(bits));
      bitset_clear_all(bits);
    }
  }

  it("can check if a bitset contains any bits of another bitset") {
    const BitSet bits = bitset_from_var((u32){0b01000100010001000100010010000000});
    check(!bitset_any_of(bits, bitset_from_var((u16){0})));
    check(!bitset_any_of(bits, bitset_from_var((u64){0})));
    check(!bitset_any_of(bits, bitset_from_var((u64){(u64)0xFF << 32})));
    check(bitset_any_of(bits, bitset_from_var((u64){(u64)0xFF << 8})));
    check(bitset_any_of(bits, bitset_from_var((u16){0xFFFF})));
    check(!bitset_any_of(bits, bitset_from_var((u16){0b0010001001001001})));
    check(bitset_any_of(bits, bitset_from_var((u16){0b0100000000000000})));
    check(bitset_any_of(bits, bitset_from_var((u16){0b0100000001001001})));
  }

  it("can check if a bitset contains all bits of another bitset") {
    const BitSet bits = bitset_from_var((u32){0b01000100010001000100010010000000});
    check(bitset_all_of(bits, bitset_from_var((u16){0})));
    check(bitset_all_of(bits, bitset_from_var((u16){0b0100010010000000})));
    check(!bitset_all_of(bits, bitset_from_var((u16){0b0100010011000000})));
    check(!bitset_all_of(bits, bitset_from_var((u16){0b0100010010000001})));
    check(!bitset_all_of(bits, bitset_from_var((u16){0b1100010010000000})));
  }

  it("returns an invalid next-bit if there are no set bits") {
    const BitSet bits = bitset_from_var((u64){0});

    check(sentinel_check(bitset_next(bits, 0)));
    check(sentinel_check(bitset_next(bits, 63)));
  }

  it("returns each bit for a mask with all bits set") {
    const BitSet bits = bitset_from_var((u64){~(u64)0});

    for (u32 i = 0; i != 64; ++i) {
      check_eq_int(bitset_next(bits, i), i);
    }
  }

  it("can compute the index of a set bit") {
    const BitSet bits = bitset_from_var((u32){0b01000100010111000100010010010011});
    check_eq_int(bitset_index(bits, 0), 0);
    check_eq_int(bitset_index(bits, 1), 1);
    check_eq_int(bitset_index(bits, 4), 2);
    check_eq_int(bitset_index(bits, 7), 3);
    check_eq_int(bitset_index(bits, 10), 4);
    check_eq_int(bitset_index(bits, 14), 5);
    check_eq_int(bitset_index(bits, 18), 6);
    check_eq_int(bitset_index(bits, 19), 7);
    check_eq_int(bitset_index(bits, 20), 8);
  }

  it("computes the same index as the iterations number while walking the set bits") {

    static u32   mask = 0b01100100010101000100010110010110;
    const BitSet bits = bitset_from_var(mask);

    usize i = 0;
    bitset_for(bits, setIdx, {
      check_eq_int(bitset_index(bits, setIdx), i);
      ++i;
    });
  }

  it("can iterate all set bits") {
    u64    val[32] = {0};
    BitSet bits    = bitset_from_array(val);

    usize indices[] = {0, 13, 42, 137, 1337, 64 * 32 - 1};

    array_for_t(indices, usize, i, { bitset_set(bits, *i); });
    check_eq_int(bitset_count(bits), array_elems(indices));

    usize i = 0;
    bitset_for(bits, setIdx, { check_eq_int(setIdx, indices[i++]); });
  }

  it("can set all bits up to a certain index") {
    usize testSizes[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 42, 55};

    u64    val[8] = {0};
    BitSet bits   = bitset_from_array(val);

    array_for_t(testSizes, usize, testSize, {
      bitset_clear_all(bits);

      bitset_set_all(bits, *testSize);
      for (usize i = 0; i != bytes_to_bits(8); ++i) {
        check(i < *testSize ? bitset_test(bits, i) : !bitset_test(bits, i));
      }
    })
  }

  it("can bitwise 'or' two bitsets") {
    BitSet evenBits64   = bitset_from_var((u64){0});
    BitSet unevenBits64 = bitset_from_var((u64){0});
    for (u32 i = 0; i != 64; ++i) {
      bitset_set(i % 2 ? unevenBits64 : evenBits64, i);
    }

    BitSet bits64 = bitset_from_var((u64){0});
    bitset_or(bits64, evenBits64);
    check_eq_int(bitset_count(bits64), 32);
    bitset_or(bits64, unevenBits64);
    check_eq_int(bitset_count(bits64), 64);

    // Check that all bits are set.
    for (u32 i = 0; i != 64; ++i) {
      check(bitset_test(bits64, i));
    }
  }

  it("can bitwise 'and' two bitsets") {
    BitSet evenBits64   = bitset_from_var((u64){0});
    BitSet unevenBits64 = bitset_from_var((u64){0});
    for (u32 i = 0; i != 64; ++i) {
      bitset_set(i % 2 ? unevenBits64 : evenBits64, i);
    }

    check_eq_int(bitset_count(evenBits64), 32);
    check_eq_int(bitset_count(unevenBits64), 32);

    bitset_set(unevenBits64, 42);

    bitset_and(evenBits64, unevenBits64);
    check_eq_int(bitset_count(evenBits64), 1);
    check_eq_int(bitset_next(evenBits64, 0), 42);
  }

  it("can bitwise 'xor' two bitsets") {
    BitSet evenBits64   = bitset_from_var((u64){0});
    BitSet unevenBits64 = bitset_from_var((u64){0});
    for (u32 i = 0; i != 64; ++i) {
      bitset_set(i % 2 ? unevenBits64 : evenBits64, i);
    }

    check_eq_int(bitset_count(evenBits64), 32);
    check_eq_int(bitset_count(unevenBits64), 32);

    BitSet bits64 = bitset_from_var((u64){0});
    bitset_xor(bits64, evenBits64);
    bitset_xor(bits64, unevenBits64);

    check_eq_int(bitset_count(bits64), 64);

    bitset_xor(bits64, evenBits64);
    check_eq_int(bitset_count(bits64), 32);

    bitset_xor(bits64, unevenBits64);
    check_eq_int(bitset_count(bits64), 0);
  }
}
