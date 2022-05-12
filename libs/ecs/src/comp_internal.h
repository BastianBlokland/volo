#pragma once
#include "core_bits.h"
#include "core_bitset.h"
#include "ecs_comp.h"

/**
 * NOTE: Intel intrinsics used for 64 bit popcnt and bit-scan-forward which have wide spread
 * support. If needed 32 bits fallback can be created.
 */
#include <immintrin.h>

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
  u32        result   = _mm_popcnt_u64(dword) - 1;
  while (dwordIdx) {
    --dwordIdx;
    result += _mm_popcnt_u64(dwords[dwordIdx]);
  }
  return result;
}

/**
 * Compute the next component identifier in the given mask.
 * Pre-condition: ecs_comp_has(mask, id)
 * Pre-condition: id is not the last component in the mask.
 */
INLINE_HINT static EcsCompId ecs_comp_next(const BitSet mask, const EcsCompId id) {
  const u64* dwords   = mask.ptr;
  u64        dwordIdx = bits_to_dwords(id);
  u64        dword    = dwords[dwordIdx] >> bit_in_dword(id);
  if (dword) {
    u64 trailing;
    _BitScanForward64(&trailing, dword);
    return id + trailing; // Aka ctz (count trailing zeroes).
  }
  for (++dwordIdx; dwordIdx != mask.size; ++dwordIdx) {
    dword = dwords[dwordIdx];
    if (dword) {
      u64 trailing;
      _BitScanForward64(&trailing, dword); // Aka ctz (count trailing zeroes).
      return dwords_to_bits(dwordIdx) + trailing;
    }
  }
  UNREACHABLE
}
