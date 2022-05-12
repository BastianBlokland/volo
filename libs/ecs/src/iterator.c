#include "core_bits.h"
#include "core_diag.h"

#include "iterator_internal.h"

#include <immintrin.h>

#define intrinsic_popcnt_64(_MASK_) _mm_popcnt_u64(_MASK_)

/**
 * Compute the index for the given component type.
 * Pre-condition: bitset_test(mask, id)
 * Pre-condition: bits_aligned(mask.size, sizeof(u64))
 */
static u64 ecs_iterator_comp_index(const BitSet mask, const EcsCompId id) {
  const u64* dwords   = mask.ptr;
  u64        dwordIdx = bits_to_dwords(id);
  const u64  dword    = dwords[dwordIdx] << (63u - bit_in_dword(id));
  u64        result   = intrinsic_popcnt_64(dword) - 1;
  while (dwordIdx) {
    --dwordIdx;
    result += intrinsic_popcnt_64(dwords[dwordIdx]);
  }
  return result;
}

EcsIterator* ecs_iterator_create(Mem mem, BitSet mask) {
  return ecs_iterator_create_with_count(mem, mask, bitset_count(mask));
}

EcsIterator* ecs_iterator_create_with_count(Mem mem, BitSet mask, usize compCount) {
  diag_assert(mem.size >= (sizeof(EcsIterator) + compCount * sizeof(Mem)));

  EcsIterator* itr = mem_as_t(mem, EcsIterator);
  *itr             = (EcsIterator){
                  .mask           = mask,
                  .compCount      = compCount,
                  .archetypeIdx   = 0,
                  .chunkIdx       = u32_max,
                  .chunkRemaining = 0,
  };
  return itr;
}

void ecs_iterator_reset(EcsIterator* itr) {
  itr->archetypeIdx   = 0;
  itr->chunkIdx       = u32_max;
  itr->chunkRemaining = 0;
}

Mem ecs_iterator_access(const EcsIterator* itr, const EcsCompId id) {
  const u32 index = ecs_iterator_comp_index(itr->mask, id);
  return itr->comps[index];
}
