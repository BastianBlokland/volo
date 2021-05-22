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
#define bytes_to_bits(_BITS_) ((_BITS_)*8)

/**
 * Retrieve the index of the given bit in its byte. Modulo 8.
 */
#define bit_in_byte(_BIT_) ((_BIT_)&0b111)

/**
 * Count how many bits are set in a mask.
 */
u32 bits_popcnt(u32);

/**
 * Count the trailing zeroes in a mask.
 * Note: returns 32 for a mask with 0 set bits.
 */
u32 bits_ctz(u32);

/**
 * Count the leading zeroes in a mask.
 * Note: returns 32 for a mask with 0 set bits.
 */
u32 bits_clz(u32);

/**
 * Check if the given value is a power of two.
 * Pre-condition: val != 0.
 */
bool bits_ispow2(u32);

/**
 * Return the next power of two greater or equal to val.
 * Pre-condition: val > 0 && val <= 2147483648.
 */
u32 bits_nextpow2(u32);

/**
 * Create a (non cryptographic) hash of the input data.
 */
u32 bits_hash32(Mem);

/**
 * Calculate the amount of padding required to reach the requested alignment.
 * Pre-condition: bits_ispow2(val) (value has to be a power of two).
 */
u32 bits_padding(u32 val, u32 align);

/**
 * Pad the given value to reach the requested alignment.
 */
u32 bits_align(u32 val, u32 align);
