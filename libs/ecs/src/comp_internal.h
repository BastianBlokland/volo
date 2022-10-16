#pragma once
#include "core_bits.h"
#include "core_bitset.h"
#include "ecs_comp.h"

#include "intrinsic_internal.h"

/**
 * Maximum supported component size.
 */
#define ecs_comp_max_size 1024

/**
 * Required alignment for a component mask.
 */
#define ecs_comp_mask_align sizeof(u64)

/**
 * Compute the required size for a component mask.
 * NOTE: Rounded up to the next dword (64 bit value).
 */
#define ecs_comp_mask_size(_DEF_) ((bits_to_dwords((_DEF_)->components.size) + 1) * sizeof(u64))

/**
 * Create component mask on the stack.
 * NOTE: The memory is not scoped, instead it always belongs to the function. So usage in a loop
 * will accumulates memory that is only freed when the function returns.
 */
#define ecs_comp_mask_stack(_DEF_) mem_stack(ecs_comp_mask_size(_DEF_))

/**
 * Test if the component is set in the given mask.
 * Pre-condition: mask.size == ecs_comp_mask_size
 */
INLINE_HINT static bool ecs_comp_has(const BitSet mask, const EcsCompId id) {
  const usize byteIdx = bits_to_bytes(id);
  return (*mem_at_u8(mask, byteIdx) & (1u << bit_in_byte(id))) != 0;
}

/**
 * Compute the index for the given component identifier.
 * Pre-condition: ecs_comp_has(mask, id)
 * Pre-condition: bits_aligned(mask.size, sizeof(u64))
 */
INLINE_HINT static u32 ecs_comp_index(const BitSet mask, const EcsCompId id) {
  const u64* dwords   = mask.ptr;
  u64        dwordIdx = bits_to_dwords(id);
  const u64  dword    = dwords[dwordIdx] << (63u - bit_in_dword(id));
  u32        result   = (u32)intrinsic_popcnt_64(dword) - 1;
  while (dwordIdx) {
    --dwordIdx;
    result += (u32)intrinsic_popcnt_64(dwords[dwordIdx]);
  }
  return result;
}

/**
 * Compute the next component identifier in the given mask.
 * Pre-condition: ecs_comp_has(mask, id)
 * Pre-condition: id is not the last component in the mask.
 * Pre-condition: bits_aligned(mask.size, sizeof(u64))
 */
INLINE_HINT static EcsCompId ecs_comp_next(const BitSet mask, const EcsCompId id) {
  const u64* dwords   = mask.ptr;
  u64        dwordIdx = bits_to_dwords(id);
  u64        dword    = dwords[dwordIdx] >> bit_in_dword(id);
  if (dword) {
    return id + intrinsic_ctz_64(dword);
  }
  for (++dwordIdx;; ++dwordIdx) {
    dword = dwords[dwordIdx];
    if (dword) {
      return dwords_to_bits(dwordIdx) + intrinsic_ctz_64(dword);
    }
  }
  UNREACHABLE
}

/**
 * Count the number of components in the mask.
 * Pre-condition: bits_aligned(mask.size, sizeof(u64))
 */
INLINE_HINT static u16 ecs_comp_mask_count(const BitSet mask) {
  u16        result    = 0;
  const u64* dwordsEnd = bits_ptr_offset(mask.ptr, mask.size);
  for (const u64* dword = mask.ptr; dword != dwordsEnd; ++dword) {
    result += intrinsic_popcnt_64(*dword);
  }
  return result;
}

/**
 * Test if two component masks are equal.
 * Pre-condition: mask.size == other.size
 * Pre-condition: bits_aligned(mask.size, sizeof(u64))
 */
INLINE_HINT static EcsCompId ecs_comp_mask_eq(const BitSet a, const BitSet b) {
  const u64* dwordsA    = a.ptr;
  const u64* dwordsAEnd = bits_ptr_offset(a.ptr, a.size);
  const u64* dwordsB    = b.ptr;
  for (; dwordsA != dwordsAEnd; ++dwordsA, ++dwordsB) {
    if (*dwordsA != *dwordsB) {
      return false;
    }
  }
  return true;
}

/**
 * Test if any of the components in the other mask also included in this mask.
 * Pre-condition: mask.size == other.size
 * Pre-condition: bits_aligned(mask.size, sizeof(u64))
 */
INLINE_HINT static bool ecs_comp_mask_any_of(const BitSet mask, const BitSet other) {
  const u64* dwordsMask    = mask.ptr;
  const u64* dwordsMaskEnd = bits_ptr_offset(mask.ptr, mask.size);
  const u64* dwordsOther   = other.ptr;
  for (; dwordsMask != dwordsMaskEnd; ++dwordsMask, ++dwordsOther) {
    if (*dwordsMask & *dwordsOther) {
      return true;
    }
  }
  return false;
}

/**
 * Test if all of the components in the other mask also included in this mask.
 * Pre-condition: mask.size == other.size
 * Pre-condition: bits_aligned(mask.size, sizeof(u64))
 */
INLINE_HINT static bool ecs_comp_mask_all_of(const BitSet mask, const BitSet other) {
  const u64* dwordsMask    = mask.ptr;
  const u64* dwordsMaskEnd = bits_ptr_offset(mask.ptr, mask.size);
  const u64* dwordsOther   = other.ptr;
  for (; dwordsMask != dwordsMaskEnd; ++dwordsMask, ++dwordsOther) {
    if ((*dwordsMask & *dwordsOther) != *dwordsOther) {
      return false;
    }
  }
  return true;
}
