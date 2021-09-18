#pragma once
#include "core_bitset.h"
#include "core_dynarray.h"

/**
 * Owning collection of bit-flags.
 * Dynamically allocates memory when required.
 * Note: Any pointers / bitsets retrieved over DynBitSet are invalidated on any mutating api.
 * Note: DynBitSets are always byte aligned, meaning size is always a multiple of 8.
 */
typedef DynArray DynBitSet;

/**
 * Create a new dynamic bitset.
 * 'capacity' (in bits) determines the size of the initial allocation, further allocations will be
 * made automatically when more memory is needed. 'capacity' of 0 is valid and won't allocate memory
 * until required.
 */
DynBitSet dynbitset_create(Allocator*, usize capacity);

/**
 * Free resources held by the dynamic-bitset.
 */
void dynbitset_destroy(DynBitSet*);

/**
 * Retrieve the current size (in bits) of the dynamic-bitset.
 */
usize dynbitset_size(const DynBitSet*);

/**
 * Retreive a bitset-view over the entire dynamic-bitset.
 * Note: This bitset is invalidated when using any of the mutating dynamic-bitset apis.
 */
BitSet dynbitset_view(const DynBitSet*);

/**
 * Set the bit at the given index.
 */
void dynbitset_set(DynBitSet*, usize idx);

/**
 * Set all the bits up to (and including) the given index.
 */
void dynbitset_set_all(DynBitSet*, usize idx);

/**
 * Set all bits which are set in the other bitset.
 */
void dynbitset_or(DynBitSet*, BitSet other);
