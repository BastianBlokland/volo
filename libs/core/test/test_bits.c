#include "check_spec.h"
#include "core_bits.h"

spec(bits) {

  it("can compute the population count of a 32 bit mask") {
    check_eq_int(bits_popcnt_32(0b00000000000000000000000000000000), 0);
    check_eq_int(bits_popcnt_32(0b00000000000000000000000000000001), 1);
    check_eq_int(bits_popcnt_32(0b10000000000000000000000000000000), 1);
    check_eq_int(bits_popcnt_32(0b00010000000000000000000000000000), 1);
    check_eq_int(bits_popcnt_32(0b00000010000000000000000001000000), 2);
    check_eq_int(bits_popcnt_32(0b10000010000000100010010001000101), 8);
    check_eq_int(bits_popcnt_32(0xFFFFFFFF), 32);
  }

  it("can compute population count of a 64 bit mask") {
    check_eq_int(
        bits_popcnt_64(0b1000001000000010001001000100010110000010000000100010010001000101), 16);
    check_eq_int(bits_popcnt_64(0xFFFFFFFFFFFFFFFF), 64);
  }

  it("can compute the amount of trailing zeroes in a 32 bit mask") {
    check_eq_int(bits_ctz_32(0b01000100010001000100010011000111), 0);
    check_eq_int(bits_ctz_32(0b01000100010001000100010011000110), 1);
    check_eq_int(bits_ctz_32(0b01000100010001000100010011000100), 2);
    check_eq_int(bits_ctz_32(0b01000100010001000100010011000000), 6);
    check_eq_int(bits_ctz_32(0b01000100010001000100010010000000), 7);
    check_eq_int(bits_ctz_32(0b01000100010001000100010000000000), 10);
    check_eq_int(bits_ctz_32(0b10000000000000000000000000000000), 31);
    check_eq_int(bits_ctz_32(0b00000000000000000000000000000000), 32);
  }

  it("can compute the amount of trailing zeroes in a 64 bit mask") {
    check_eq_int(bits_ctz_64(0xFFFFFFFFFFFFFFFF), 0);
    check_eq_int(bits_ctz_64(1), 0);
    check_eq_int(
        bits_ctz_64(0b0100010001000100010001001100000001000100010001000100010011000000), 6);
    check_eq_int(
        bits_ctz_64(0b0100010001000100010001001000000000000000000000000000000000000000), 39);
    check_eq_int(bits_ctz_64(0), 64);
  }

  it("can compute the amount of leading zeroes in a 32 bit mask") {
    check_eq_int(bits_clz_32(0b11000100010001000100010011000100), 0);
    check_eq_int(bits_clz_32(0b01000100010001000100010011000101), 1);
    check_eq_int(bits_clz_32(0b00111100010001000100010011000100), 2);
    check_eq_int(bits_clz_32(0b00000010011001000100010011001100), 6);
    check_eq_int(bits_clz_32(0b00000001110001000100010010000011), 7);
    check_eq_int(bits_clz_32(0b00000000001101000100010000010000), 10);
    check_eq_int(bits_clz_32(0b00000000000000000000000000000001), 31);
    check_eq_int(bits_clz_32(0b00000000000000000000000000000000), 32);
  }

  it("can compute the amount of leading zeroes in a 64 bit mask") {
    check_eq_int(bits_clz_64(0xFFFFFFFFFFFFFFFF), 0);
    check_eq_int(bits_clz_64(1), 63);
    check_eq_int(bits_clz_64(0b010001000100010001), 47);
    check_eq_int(bits_clz_64(0), 64);
  }

  it("can check if a 32 bit integer is a power-of-two") {
    // Undefined for val,0.
    check(bits_ispow2_32(1));
    check(bits_ispow2_32(2));
    check(!bits_ispow2_32(3));
    check(bits_ispow2_32(4));
    check(!bits_ispow2_32(5));
    check(!bits_ispow2_32(6));
    check(!bits_ispow2_32(7));
    check(bits_ispow2_32(8));
    check(!bits_ispow2_32(9));
    check(bits_ispow2_32(16));
    check(bits_ispow2_32(32));
    check(!bits_ispow2_32(63));
    check(bits_ispow2_32(128));
    check(!bits_ispow2_32(2147483647));
    check(bits_ispow2_32(2147483648));
  }

  it("can check if a 64 bit integer is a power-of-two") {
    // Undefined for val,0.
    check(bits_ispow2_64(128));
    check(!bits_ispow2_64(2147483647));
    check(bits_ispow2_64(2147483648));
    check(!bits_ispow2_64(4294967295));
    check(bits_ispow2_64(4294967296));
    check(!bits_ispow2_64(34359738367));
    check(bits_ispow2_64(34359738368));
    check(!bits_ispow2_64(68719476735));
    check(bits_ispow2_64(68719476736));
    check(bits_ispow2_64(u64_lit(9223372036854775808)));
  }

  it("can compute the next power-of-two for a 32 bit integer") {
    // Undefined for val,0.
    check_eq_int(bits_nextpow2_32(1), 1);
    check_eq_int(bits_nextpow2_32(2), 2);
    check_eq_int(bits_nextpow2_32(3), 4);
    check_eq_int(bits_nextpow2_32(4), 4);
    check_eq_int(bits_nextpow2_32(5), 8);
    check_eq_int(bits_nextpow2_32(6), 8);
    check_eq_int(bits_nextpow2_32(7), 8);
    check_eq_int(bits_nextpow2_32(8), 8);
    check_eq_int(bits_nextpow2_32(9), 16);
    check_eq_int(bits_nextpow2_32(16), 16);
    check_eq_int(bits_nextpow2_32(32), 32);
    check_eq_int(bits_nextpow2_32(63), 64);
    check_eq_int(bits_nextpow2_32(128), 128);
    check_eq_int(bits_nextpow2_32(255), 256);
    check_eq_int(bits_nextpow2_32(257), 512);
    check_eq_int(bits_nextpow2_32(4096), 4096);
    check_eq_int(bits_nextpow2_32(u32_lit(2147483647)), u32_lit(2147483648));
    check_eq_int(bits_nextpow2_32(u32_lit(2147483648)), u32_lit(2147483648));
    // Undefined for val > 2147483648.
  }

  it("can compute the next power-of-two for a 64 bit integer") {
    // Undefined for val,0.
    check_eq_int(bits_nextpow2_64(u64_lit(128)), u64_lit(128));
    check_eq_int(bits_nextpow2_64(u64_lit(255)), u64_lit(256));
    check_eq_int(bits_nextpow2_64(u64_lit(257)), u64_lit(512));
    check_eq_int(bits_nextpow2_64(u64_lit(4096)), u64_lit(4096));
    check_eq_int(bits_nextpow2_64(u64_lit(2147483647)), u64_lit(2147483648));
    check_eq_int(bits_nextpow2_64(u64_lit(68719476735)), u64_lit(68719476736));
    check_eq_int(bits_nextpow2_64(u64_lit(68719476736)), u64_lit(68719476736));
    check_eq_int(bits_nextpow2_64(u64_lit(9223372036854775807)), u64_lit(9223372036854775808));
    check_eq_int(bits_nextpow2_64(u64_lit(9223372036854775808)), u64_lit(9223372036854775808));
    // Undefined for val > 9223372036854775808.
  }

  it("can compute the amount of padding required to align a 32 bit integer") {
    check_eq_int(bits_padding_32(0, 4), 0);
    check_eq_int(bits_padding_32(4, 4), 0);
    check_eq_int(bits_padding_32(1, 4), 3);
    check_eq_int(bits_padding_32(2, 4), 2);
    check_eq_int(bits_padding_32(3, 4), 1);
  }

  it("can align a 32 bit integer") {
    check_eq_int(bits_align_32(0, 4), 0);
    check_eq_int(bits_align_32(1, 4), 4);
    check_eq_int(bits_align_32(4, 4), 4);
    check_eq_int(bits_align_32(5, 4), 8);
    check_eq_int(bits_align_32(31, 4), 32);
  }

  it("can align an integer to pointer alignment") {
    check_eq_int(bits_align_ptr(1u), 8);
    check_eq_int(bits_align_ptr(7u), 8);
    check_eq_int(bits_align_ptr(11u), 16);
  }

  it("can check if a value satisfies given alignment") {
    check(bits_aligned(0u, 8u));
    check(bits_aligned(8u, 8u));
    check(bits_aligned(16u, 8u));
    check(bits_aligned(32u, 8u));
    check(bits_aligned(bits_align_ptr(1u), sizeof(void*)));

    check(!bits_aligned(1u, 8u));
    check(!bits_aligned(7u, 8u));
    check(!bits_aligned(9u, 8u));
    check(!bits_aligned(31u, 8u));
  }

  it("can alias unsigned integers and floats") {
    check_eq_float(bits_u32_as_f32(bits_f32_as_u32(1.337f)), 1.337f, 1e-6f);
    check_eq_int(bits_f32_as_u32(bits_u32_as_f32(42)), 42);

    check_eq_float(bits_u64_as_f64(bits_f64_as_u64(1.337)), 1.337, 1e-6f);
    check_eq_int(bits_f64_as_u64(bits_u64_as_f64(42)), 42);
  }
}
