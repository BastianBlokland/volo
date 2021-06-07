#include "core_bits.h"
#include "core_diag.h"

static void test_bits_popcnt_32() {
  diag_assert(bits_popcnt_32(0b00000000000000000000000000000000) == 0);
  diag_assert(bits_popcnt_32(0b00000000000000000000000000000001) == 1);
  diag_assert(bits_popcnt_32(0b10000000000000000000000000000000) == 1);
  diag_assert(bits_popcnt_32(0b00010000000000000000000000000000) == 1);
  diag_assert(bits_popcnt_32(0b00000010000000000000000001000000) == 2);
  diag_assert(bits_popcnt_32(0b10000010000000100010010001000101) == 8);
  diag_assert(bits_popcnt_32(0xFFFFFFFF) == 32);
}

static void test_bits_popcnt_64() {
  diag_assert(
      bits_popcnt_64(0b1000001000000010001001000100010110000010000000100010010001000101) == 16);
  diag_assert(bits_popcnt_64(0xFFFFFFFFFFFFFFFF) == 64);
}

static void test_bits_ctz_32() {
  diag_assert(bits_ctz_32(0b01000100010001000100010011000111) == 0);
  diag_assert(bits_ctz_32(0b01000100010001000100010011000110) == 1);
  diag_assert(bits_ctz_32(0b01000100010001000100010011000100) == 2);
  diag_assert(bits_ctz_32(0b01000100010001000100010011000000) == 6);
  diag_assert(bits_ctz_32(0b01000100010001000100010010000000) == 7);
  diag_assert(bits_ctz_32(0b01000100010001000100010000000000) == 10);
  diag_assert(bits_ctz_32(0b10000000000000000000000000000000) == 31);
  diag_assert(bits_ctz_32(0b00000000000000000000000000000000) == 32);
}

static void test_bits_ctz_64() {
  diag_assert(bits_ctz_64(0xFFFFFFFFFFFFFFFF) == 0);
  diag_assert(bits_ctz_64(1) == 0);
  diag_assert(bits_ctz_64(0b0100010001000100010001001100000001000100010001000100010011000000) == 6);
  diag_assert(
      bits_ctz_64(0b0100010001000100010001001000000000000000000000000000000000000000) == 39);
  diag_assert(bits_ctz_64(0) == 64);
}

static void test_bits_clz_32() {
  diag_assert(bits_clz_32(0b11000100010001000100010011000100) == 0);
  diag_assert(bits_clz_32(0b01000100010001000100010011000101) == 1);
  diag_assert(bits_clz_32(0b00111100010001000100010011000100) == 2);
  diag_assert(bits_clz_32(0b00000010011001000100010011001100) == 6);
  diag_assert(bits_clz_32(0b00000001110001000100010010000011) == 7);
  diag_assert(bits_clz_32(0b00000000001101000100010000010000) == 10);
  diag_assert(bits_clz_32(0b00000000000000000000000000000001) == 31);
  diag_assert(bits_clz_32(0b00000000000000000000000000000000) == 32);
}

static void test_bits_clz_64() {
  diag_assert(bits_clz_64(0xFFFFFFFFFFFFFFFF) == 0);
  diag_assert(bits_clz_64(1) == 63);
  diag_assert(bits_clz_64(0b010001000100010001) == 47);
  diag_assert(bits_clz_64(0) == 64);
}

static void test_bits_ispow2_32() {
  // Undefined for val == 0.
  diag_assert(bits_ispow2_32(1));
  diag_assert(bits_ispow2_32(2));
  diag_assert(!bits_ispow2_32(3));
  diag_assert(bits_ispow2_32(4));
  diag_assert(!bits_ispow2_32(5));
  diag_assert(!bits_ispow2_32(6));
  diag_assert(!bits_ispow2_32(7));
  diag_assert(bits_ispow2_32(8));
  diag_assert(!bits_ispow2_32(9));
  diag_assert(bits_ispow2_32(16));
  diag_assert(bits_ispow2_32(32));
  diag_assert(!bits_ispow2_32(63));
  diag_assert(bits_ispow2_32(128));
  diag_assert(!bits_ispow2_32(2147483647));
  diag_assert(bits_ispow2_32(2147483648));
}

static void test_bits_ispow2_64() {
  // Undefined for val == 0.
  diag_assert(bits_ispow2_64(128));
  diag_assert(!bits_ispow2_64(2147483647));
  diag_assert(bits_ispow2_64(2147483648));
  diag_assert(!bits_ispow2_64(4294967295));
  diag_assert(bits_ispow2_64(4294967296));
  diag_assert(!bits_ispow2_64(34359738367));
  diag_assert(bits_ispow2_64(34359738368));
  diag_assert(!bits_ispow2_64(68719476735));
  diag_assert(bits_ispow2_64(68719476736));
  diag_assert(bits_ispow2_64(9223372036854775808ull));
}

static void test_bits_nextpow2_32() {
  // Undefined for val == 0.
  diag_assert(bits_nextpow2_32(1) == 1);
  diag_assert(bits_nextpow2_32(2) == 2);
  diag_assert(bits_nextpow2_32(3) == 4);
  diag_assert(bits_nextpow2_32(4) == 4);
  diag_assert(bits_nextpow2_32(5) == 8);
  diag_assert(bits_nextpow2_32(6) == 8);
  diag_assert(bits_nextpow2_32(7) == 8);
  diag_assert(bits_nextpow2_32(8) == 8);
  diag_assert(bits_nextpow2_32(9) == 16);
  diag_assert(bits_nextpow2_32(16) == 16);
  diag_assert(bits_nextpow2_32(32) == 32);
  diag_assert(bits_nextpow2_32(63) == 64);
  diag_assert(bits_nextpow2_32(128) == 128);
  diag_assert(bits_nextpow2_32(255) == 256);
  diag_assert(bits_nextpow2_32(257) == 512);
  diag_assert(bits_nextpow2_32(4096) == 4096);
  diag_assert(bits_nextpow2_32(2147483647) == 2147483648);
  diag_assert(bits_nextpow2_32(2147483648) == 2147483648);
  // Undefined for val > 2147483648.
}

static void test_bits_nextpow2_64() {
  // Undefined for val == 0.
  diag_assert(bits_nextpow2_64(128) == 128);
  diag_assert(bits_nextpow2_64(255) == 256);
  diag_assert(bits_nextpow2_64(257) == 512);
  diag_assert(bits_nextpow2_64(4096) == 4096);
  diag_assert(bits_nextpow2_64(2147483647) == 2147483648);
  diag_assert(bits_nextpow2_64(68719476735) == 68719476736);
  diag_assert(bits_nextpow2_64(68719476736) == 68719476736);
  diag_assert(bits_nextpow2_64(9223372036854775807ull) == 9223372036854775808ull);
  diag_assert(bits_nextpow2_64(9223372036854775808ull) == 9223372036854775808ull);
  // Undefined for val > 9223372036854775808ull.
}

static void test_bits_padding_32() {
  diag_assert(bits_padding_32(0, 4) == 0);
  diag_assert(bits_padding_32(4, 4) == 0);
  diag_assert(bits_padding_32(1, 4) == 3);
  diag_assert(bits_padding_32(2, 4) == 2);
  diag_assert(bits_padding_32(3, 4) == 1);
}

static void test_bits_align_32() {
  diag_assert(bits_align_32(0, 4) == 0);
  diag_assert(bits_align_32(1, 4) == 4);
  diag_assert(bits_align_32(4, 4) == 4);
  diag_assert(bits_align_32(5, 4) == 8);
  diag_assert(bits_align_32(31, 4) == 32);
}

static void test_bits_float_conversions() {
  diag_assert(bits_u32_as_f32(bits_f32_as_u32(1.337f)) == 1.337f);
  diag_assert(bits_f32_as_u32(bits_u32_as_f32(42)) == 42);

  diag_assert(bits_u64_as_f64(bits_f64_as_u64(1.337)) == 1.337);
  diag_assert(bits_f64_as_u64(bits_u64_as_f64(42)) == 42);
}

void test_bits() {
  test_bits_popcnt_32();
  test_bits_popcnt_64();
  test_bits_ctz_32();
  test_bits_ctz_64();
  test_bits_clz_32();
  test_bits_clz_64();
  test_bits_ispow2_32();
  test_bits_ispow2_64();
  test_bits_nextpow2_32();
  test_bits_nextpow2_64();
  test_bits_padding_32();
  test_bits_align_32();
  test_bits_float_conversions();
}
