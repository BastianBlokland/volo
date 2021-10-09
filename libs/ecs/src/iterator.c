#include "core_diag.h"

#include "iterator_internal.h"

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

Mem ecs_iterator_access(EcsIterator* itr, const EcsCompId id) {
  return itr->comps[bitset_index(itr->mask, id)];
}
