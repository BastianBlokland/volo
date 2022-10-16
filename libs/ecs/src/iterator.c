#include "core_bits.h"
#include "core_diag.h"

#include "iterator_internal.h"

EcsIterator* ecs_iterator_create(const Mem mem, const BitSet mask) {
  return ecs_iterator_create_with_count(mem, mask, ecs_comp_mask_count(mask));
}

EcsIterator* ecs_iterator_create_with_count(const Mem mem, const BitSet mask, const u16 compCount) {
  diag_assert(mem.size >= (sizeof(EcsIterator) + compCount * sizeof(Mem)));

  EcsIterator* itr = mem_as_t(mem, EcsIterator);

  *itr = (EcsIterator){
      .compCount            = compCount,
      .archetypeIdx         = 0,
      .chunksLimitRemaining = u16_max,
      .chunkIdx             = u32_max,
      .chunkRemaining       = 0,
      .mask                 = mask,
  };
  return itr;
}

void ecs_iterator_reset(EcsIterator* itr) {
  itr->archetypeIdx   = 0;
  itr->chunkIdx       = u32_max;
  itr->chunkRemaining = 0;
}
