#include "core_array.h"
#include "core_bitset.h"
#include "core_diag.h"

static void test_bitset_test_no_bits_set() {
  const BitSet zero64 = bitset_from_var((u64){0});

  diag_assert(bitset_size(zero64) == 64);
  diag_assert(bitset_count(zero64) == 0);
  for (u32 i = 0; i != 64; ++i) {
    diag_assert(!bitset_test(zero64, i));
  }
}

static void test_bitset_test_all_bits_set() {
  const BitSet ones64 = bitset_from_var((u64){~(u64)0});

  diag_assert(bitset_size(ones64) == 64);
  diag_assert(bitset_count(ones64) == 64);
  for (u32 i = 0; i != 64; ++i) {
    diag_assert(bitset_test(ones64, i));
  }
}

static void test_bitset_test_finds_set_bit() {
  u64    val[32] = {0};
  BitSet bits    = bitset_from_array(val);

  diag_assert(bitset_size(bits) == 64 * 32);

  // Check no bit is set.
  diag_assert(bitset_count(bits) == 0);
  for (u32 i = 0; i != 64; ++i) {
    diag_assert(!bitset_test(bits, i));
  }

  bitset_set(bits, 1337);
  diag_assert(bitset_test(bits, 1337));
  diag_assert(bitset_count(bits) == 1);

  bitset_set(bits, 42);
  diag_assert(bitset_test(bits, 42));
  diag_assert(bitset_count(bits) == 2);

  // Clear the set bits.
  bitset_clear(bits, 42);
  bitset_clear(bits, 1337);

  // Check no bit is set.
  diag_assert(bitset_count(bits) == 0);
  for (u32 i = 0; i != 64; ++i) {
    diag_assert(!bitset_test(bits, i));
  }
}

static void test_bitset_count() {
  BitSet bits = bitset_from_var((u64){0});

  bitset_set(bits, 0);
  bitset_set(bits, 63);
  bitset_set(bits, 42);
  bitset_set(bits, 13);
  bitset_set(bits, 51);

  diag_assert(bitset_count(bits) == 5);
}

static void test_bitset_any() {
  BitSet bits = bitset_from_var((u64){0});
  for (u32 i = 0; i != 64; ++i) {
    diag_assert(!bitset_any(bits));
    bitset_set(bits, i);
    diag_assert(bitset_any(bits));
    bitset_clear_all(bits);
  }
}

static void test_bitset_any_of() {
  const BitSet bits = bitset_from_var((u32){0b01000100010001000100010010000000});
  diag_assert(!bitset_any_of(bits, bitset_from_var((u16){0})));
  diag_assert(!bitset_any_of(bits, bitset_from_var((u64){0})));
  diag_assert(!bitset_any_of(bits, bitset_from_var((u64){(u64)0xFF << 32})));
  diag_assert(bitset_any_of(bits, bitset_from_var((u64){(u64)0xFF << 8})));
  diag_assert(bitset_any_of(bits, bitset_from_var((u16){0xFFFF})));
  diag_assert(!bitset_any_of(bits, bitset_from_var((u16){0b0010001001001001})));
  diag_assert(bitset_any_of(bits, bitset_from_var((u16){0b0100000000000000})));
  diag_assert(bitset_any_of(bits, bitset_from_var((u16){0b0100000001001001})));
}

static void test_bitset_all_of() {
  const BitSet bits = bitset_from_var((u32){0b01000100010001000100010010000000});
  diag_assert(bitset_all_of(bits, bitset_from_var((u16){0})));
  diag_assert(bitset_all_of(bits, bitset_from_var((u16){0b0100010010000000})));
  diag_assert(!bitset_all_of(bits, bitset_from_var((u16){0b0100010011000000})));
  diag_assert(!bitset_all_of(bits, bitset_from_var((u16){0b0100010010000001})));
  diag_assert(!bitset_all_of(bits, bitset_from_var((u16){0b1100010010000000})));
}

static void test_bitset_next_returns_none_for_zero() {
  const BitSet bits = bitset_from_var((u64){0});

  diag_assert(sentinel_check(bitset_next(bits, 0)));
  diag_assert(sentinel_check(bitset_next(bits, 63)));
}

static void test_bitset_next_returns_each_bit_for_all_ones() {
  const BitSet bits = bitset_from_var((u64){~(u64)0});

  for (u32 i = 0; i != 64; ++i) {
    diag_assert(bitset_next(bits, i) == i);
  }
}

static void test_bitset_index() {
  const BitSet bits = bitset_from_var((u32){0b01000100010111000100010010010011});
  diag_assert(bitset_index(bits, 0) == 0);
  diag_assert(bitset_index(bits, 1) == 1);
  diag_assert(bitset_index(bits, 4) == 2);
  diag_assert(bitset_index(bits, 7) == 3);
  diag_assert(bitset_index(bits, 10) == 4);
  diag_assert(bitset_index(bits, 14) == 5);
  diag_assert(bitset_index(bits, 18) == 6);
  diag_assert(bitset_index(bits, 19) == 7);
  diag_assert(bitset_index(bits, 20) == 8);
}

static void test_bitset_index_matches_iteration_n(const u32 mask) {
  const BitSet bits = bitset_from_var(mask);

  usize i = 0;
  bitset_for(bits, setIdx, {
    diag_assert(bitset_index(bits, setIdx) == i);
    ++i;
  });
}

static void test_bitset_iterate_returns_all_set_bits() {
  u64    val[32] = {0};
  BitSet bits    = bitset_from_array(val);

  usize indices[] = {0, 13, 42, 137, 1337, 64 * 32 - 1};

  array_for_t(indices, usize, i, { bitset_set(bits, *i); });
  diag_assert(bitset_count(bits) == array_elems(indices));

  usize i = 0;
  bitset_for(bits, setIdx, { diag_assert(setIdx == indices[i++]); });
}

static void test_bitset_or() {
  BitSet evenBits64   = bitset_from_var((u64){0});
  BitSet unevenBits64 = bitset_from_var((u64){0});
  for (u32 i = 0; i != 64; ++i) {
    bitset_set(i % 2 ? unevenBits64 : evenBits64, i);
  }

  BitSet bits64 = bitset_from_var((u64){0});
  bitset_or(bits64, evenBits64);
  diag_assert(bitset_count(bits64) == 32);
  bitset_or(bits64, unevenBits64);
  diag_assert(bitset_count(bits64) == 64);

  // Check that all bits are set.
  for (u32 i = 0; i != 64; ++i) {
    diag_assert(bitset_test(bits64, i));
  }
}

static void test_bitset_and() {
  BitSet evenBits64   = bitset_from_var((u64){0});
  BitSet unevenBits64 = bitset_from_var((u64){0});
  for (u32 i = 0; i != 64; ++i) {
    bitset_set(i % 2 ? unevenBits64 : evenBits64, i);
  }

  diag_assert(bitset_count(evenBits64) == 32);
  diag_assert(bitset_count(unevenBits64) == 32);

  bitset_set(unevenBits64, 42);

  bitset_and(evenBits64, unevenBits64);
  diag_assert(bitset_count(evenBits64) == 1);
  diag_assert(bitset_next(evenBits64, 0) == 42);
}

void test_bitset() {
  test_bitset_test_no_bits_set();
  test_bitset_test_all_bits_set();
  test_bitset_test_finds_set_bit();
  test_bitset_count();
  test_bitset_any();
  test_bitset_any_of();
  test_bitset_all_of();
  test_bitset_next_returns_none_for_zero();
  test_bitset_next_returns_each_bit_for_all_ones();
  test_bitset_index();
  test_bitset_index_matches_iteration_n(0xFFFFFFFF);
  test_bitset_index_matches_iteration_n(0b01100100010101000100010110010110);
  test_bitset_iterate_returns_all_set_bits();
  test_bitset_or();
  test_bitset_and();
}
