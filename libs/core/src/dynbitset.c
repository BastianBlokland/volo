#include "core_bits.h"
#include "core_dynbitset.h"

#include "intrinsic_internal.h"

#define dynbitset_align sizeof(u64)

static usize bitset_required_bytes(const usize bit) {
  return (bits_to_dwords(bit) + 1) * sizeof(u64);
}

static void dynbitset_ensure(DynBitSet* dynbitset, const usize bit) {
  const usize byte = (bits_to_dwords(bit) + 1) * sizeof(u64);
  if (byte >= dynbitset->size) {
    // Out of bounds, add new bytes and initialize them to 0.
    mem_set(dynarray_push(dynbitset, byte - dynbitset->size), 0);
  }
}

DynBitSet dynbitset_create(Allocator* alloc, const usize capacity) {
  return dynarray_create(alloc, 1, dynbitset_align, bitset_required_bytes(capacity));
}

void dynbitset_destroy(DynBitSet* dynbitset) { dynarray_destroy(dynbitset); }

usize dynbitset_size(const DynBitSet* dynbitset) { return bytes_to_bits(dynbitset->size); }

usize dynbitset_count(const DynBitSet* dynbitset) {
  usize       result    = 0;
  const u64*  dwords    = dynbitset->data.ptr;
  const usize wordCount = dynbitset->size / sizeof(u64);
  for (usize i = 0; i != wordCount; ++i) {
    result += intrinsic_popcnt_64(dwords[i]);
  }
  return result;
}

BitSet dynbitset_view(const DynBitSet* dynbitset) {
  return mem_create(bits_ptr_offset(dynbitset->data.ptr, 0), dynbitset->size);
}

bool dynbitset_test(const DynBitSet* dynbitset, const usize idx) {
  const usize byteIdx = bits_to_bytes(idx);
  if (byteIdx >= dynbitset->size) {
    return false;
  }
  return (*mem_at_u8(dynbitset_view(dynbitset), byteIdx) & (1u << bit_in_byte(idx))) != 0;
}

usize dynbitset_next(const DynBitSet* dynbitset, const usize idx) {
  if (UNLIKELY(idx >= bytes_to_bits(dynbitset->size))) {
    return sentinel_usize;
  }
  const u64* dwords   = dynbitset->data.ptr;
  u64        dwordIdx = bits_to_dwords(idx);
  u64        dword    = dwords[dwordIdx] >> bit_in_dword(idx);
  if (dword) {
    return idx + intrinsic_ctz_64(dword);
  }
  for (++dwordIdx; dwordIdx * sizeof(u64) != dynbitset->size; ++dwordIdx) {
    dword = dwords[dwordIdx];
    if (dword) {
      return dwords_to_bits(dwordIdx) + intrinsic_ctz_64(dword);
    }
  }
  return sentinel_usize;
}

void dynbitset_set(DynBitSet* dynbitset, const usize idx) {
  dynbitset_ensure(dynbitset, idx);
  *mem_at_u8(dynbitset_view(dynbitset), bits_to_bytes(idx)) |= 1u << bit_in_byte(idx);
}

void dynbitset_set_all(DynBitSet* dynbitset, const usize idx) {
  dynbitset_ensure(dynbitset, idx);
  bitset_set_all(dynarray_at(dynbitset, 0, bits_to_bytes(idx) + 1), idx);
}

void dynbitset_clear(DynBitSet* dynbitset, const usize idx) {
  dynbitset_ensure(dynbitset, idx);
  *mem_at_u8(dynbitset_view(dynbitset), bits_to_bytes(idx)) &= ~(1u << bit_in_byte(idx));
}

void dynbitset_or(DynBitSet* dynbitset, const BitSet other) {
  dynbitset_ensure(dynbitset, bytes_to_bits(other.size));
  bitset_or(dynbitset_view(dynbitset), other);
}
