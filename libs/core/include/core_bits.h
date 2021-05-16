#pragma once
#include "core_memory.h"
#include "core_types.h"

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
 * Undefined for val == 0.
 */
bool bits_ispow2(u32);

/**
 * Return the next power of two greater or equal to val.
 * Undefined for val == 0 and val > 2147483648
 */
u32 bits_nextpow2(u32);

/**
 * Create a (non cryptographic) hash of the input data.
 */
u32 bits_hash32(Mem);

/**
 * Calculate the amount of padding required to reach the requested alignment.
 * Undefined if align is not a power of 2.
 */
u32 bits_padding(u32 val, u32 align);

/**
 * Pad the given value to reach the requested alignment.
 */
u32 bits_align(u32 val, u32 align);
