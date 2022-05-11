#include "core_bits.h"
#include "core_diag.h"

#include "iterator_internal.h"

#if defined(VOLO_MSVC)
#define intrinsic_popcnt_32(_MASK_) __popcnt(_MASK_)
#else
#define intrinsic_popcnt_32(_MASK_) __builtin_popcount(_MASK_);
#endif

/**
 * Compute the index for the given component type.
 * Pre-condition: bitset_test(mask, id)
 * Pre-condition: bits_aligned(mask.size, sizeof(u32))
 */
static u32 ecs_iterator_comp_index(const BitSet mask, const EcsCompId id) {
  const u32* words   = mask.ptr;
  u32        wordIdx = bits_to_words(id);
  const u32  word    = words[wordIdx] << (31u - bit_in_word(id));
  u32        result  = intrinsic_popcnt_32(word) - 1;
  while (wordIdx) {
    --wordIdx;
    result += intrinsic_popcnt_32(words[wordIdx]);
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
