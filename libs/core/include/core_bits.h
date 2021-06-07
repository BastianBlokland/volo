#pragma once
#include "core_memory.h"
#include "core_types.h"

/**
 * Convert number of bits to number of bytes. Divide by 8.
 */
#define bits_to_bytes(_BITS_) ((_BITS_) >> 3)

/**
 * Convert number of bytes to bits. Multiply by 8.
 */
#define bytes_to_bits(_BYTES_) ((_BYTES_)*8)

/**
 * Retrieve the index of the given bit in its byte. Modulo 8.
 */
#define bit_in_byte(_BIT_) ((_BIT_)&0b111)

/**
 * Count how many bits are set in a mask.
 */
u8 bits_popcnt_32(u32);
u8 bits_popcnt_64(u64);

/**
 * Count the trailing zeroes in a mask.
 * Note: returns 32 / 64 for a mask with 0 set bits.
 */
u8 bits_ctz_32(u32);
u8 bits_ctz_64(u64);

/**
 * Count the leading zeroes in a mask.
 * Note: returns 32 / 64 for a mask with 0 set bits.
 */
u8 bits_clz_32(u32);
u8 bits_clz_64(u64);

/**
 * Check if the given value is a power of two.
 * Pre-condition: val != 0.
 */
bool bits_ispow2_32(u32);
bool bits_ispow2_64(u64);

/**
 * Return the next power of two greater or equal to val.
 * Pre-condition: val > 0
 * Pre-condition for 32 bit: val <= 2147483648.
 * Pre-condition for 64 bit: val <= 9223372036854775808.
 */
u32 bits_nextpow2_32(u32);
u64 bits_nextpow2_64(u64);

/**
 * Create a (non cryptographic) hash of the input data.
 */
u32 bits_hash_32(Mem);

/**
 * Calculate the amount of padding required to reach the requested alignment.
 * Pre-condition: bits_ispow2(val) (value has to be a power of two).
 */
u32 bits_padding_32(u32 val, u32 align);
u64 bits_padding_64(u64 val, u64 align);

/**
 * Pad the given value to reach the requested alignment.
 */
u32 bits_align_32(u32 val, u32 align);
u64 bits_align_64(u64 val, u64 align);

/**
 * Reinterpret the 32 bit integer as a floating point value.
 */
f32 bits_u32_as_f32(u32);

/**
 * Reinterpret the 32 bit floating point value as an integer value.
 */
u32 bits_f32_as_u32(f32);

/**
 * Reinterpret the 64 bit integer as a floating point value.
 */
f64 bits_u64_as_f64(u64);

/**
 * Reinterpret the 64 bit floating point value as an integer value.
 */
u64 bits_f64_as_u64(f64);
