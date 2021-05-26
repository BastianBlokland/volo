#pragma once
#include "core_memory.h"
#include "core_sentinel.h"

/**
 * Non-owning view over memory containing bit flags.
 * Note: BitSets are always byte aligned, meaning size is always a multiple of 8.
 */
typedef Mem BitSet;

/**
 * Create bitset view over a variable.
 */
#define bitset_from_var(_VAR_)                                                                     \
  ((BitSet){                                                                                       \
      .ptr  = (void*)&(_VAR_),                                                                     \
      .size = sizeof(_VAR_),                                                                       \
  })

/**
 * Create bitset view over an array.
 */
#define bitset_from_array(_ARRAY_)                                                                 \
  ((BitSet){                                                                                       \
      .ptr  = (void*)(_ARRAY_),                                                                    \
      .size = sizeof(_ARRAY_),                                                                     \
  })

/**
 * Iterate over all the set bits in a bitset.
 */
#define bitset_for(_BITSET_, _VAR_, ...)                                                           \
  {                                                                                                \
    const usize _VAR_##_size = bitset_size(_BITSET_);                                              \
    usize       _VAR_        = 0;                                                                  \
    do {                                                                                           \
      if (sentinel_check(_VAR_ = bitset_next(_BITSET_, _VAR_))) {                                  \
        break;                                                                                     \
      }                                                                                            \
      __VA_ARGS__                                                                                  \
    } while (++_VAR_ < _VAR_##_size);                                                              \
  }

/**
 * Return the total bit count (either set or unset) in the bitset.
 */
usize bitset_size(BitSet);

/**
 * Test if the bit at the given index is set.
 * Pre-condition: idx < bitset_size
 */
bool bitset_test(BitSet, usize idx);

/**
 * Count all the set bits.
 */
usize bitset_count(BitSet);

/**
 * Are any bits set?
 */
bool bitset_any(BitSet);

/**
 * Are any of the set bits in the other BitSet also set in this one?
 */
bool bitset_any_of(BitSet, BitSet other);

/**
 * Are all of the set bits in the other BitSet also set in this one?
 * Pre-condition: bitset_size(other) <= bitset_size
 */
bool bitset_all_of(BitSet, BitSet other);

/**
 * Return the next set bit starting from the given index.
 * Pre-condition: idx < bitset_size
 * Note: Returns 'sentinel_usize' if there are no more set bits.
 */
usize bitset_next(BitSet, usize idx);

/**
 * Return the index of the given set bit in the set bit collection.
 * - Example: Returns 0 if idx is the first set bit.
 * - Example: Returns 1 if idx is the second set bit.
 *
 * Pre-condition: bitset_test(idx)
 */
usize bitset_index(BitSet, usize idx);

/**
 * Set the bit at the given index.
 * Pre-condition: idx < bitset_size
 */
void bitset_set(BitSet, usize idx);

/**
 * Unset the bit at the given index.
 * Pre-condition: idx < bitset_size
 */
void bitset_clear(BitSet, usize idx);

/**
 * Clear all bits.
 */
void bitset_clear_all(BitSet);

/**
 * Set all bits which are set in the other bitset.
 * Pre-condition: bitset_size(other) <= bitset_size
 */
void bitset_or(BitSet, BitSet other);

/**
 * Clear all bits which are not set in the other bitset.
 * Pre-condition: bitset_size(other) >= bitset_size
 */
void bitset_and(BitSet, BitSet other);
