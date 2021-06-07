#include "core_bits.h"
#include "core_dynbitset.h"

static usize bitset_required_bytes(usize bit) {
  return bits_to_bytes(bit) + (bit_in_byte(bit) ? 1 : 0);
}

static void dynbitset_ensure(DynBitSet* dynbitset, usize bit) {
  const usize byte = bits_to_bytes(bit);
  if (byte >= dynbitset->size) {
    // Out of bounds, add new bytes and intiialize them to 0.
    mem_set(dynarray_push(dynbitset, 1 + byte - dynbitset->size), 0);
  }
}

DynBitSet dynbitset_create(Allocator* alloc, usize capacity) {
  return dynarray_create(alloc, 1u, bitset_required_bytes(capacity));
}

void dynbitset_destroy(DynBitSet* dynbitset) { dynarray_destroy(dynbitset); }

usize dynbitset_size(const DynBitSet* dynbitset) { return bytes_to_bits(dynbitset->size); }

BitSet dynbitset_view(const DynBitSet* dynbitset) {
  return dynarray_at(dynbitset, 0, dynbitset->size);
}

void dynbitset_set(DynBitSet* dynbitset, usize idx) {
  dynbitset_ensure(dynbitset, idx);
  bitset_set(dynbitset_view(dynbitset), idx);
}

void dynbitset_or(DynBitSet* dynbitset, BitSet other) {
  dynbitset_ensure(dynbitset, bytes_to_bits(other.size));
  bitset_or(dynbitset_view(dynbitset), other);
}
